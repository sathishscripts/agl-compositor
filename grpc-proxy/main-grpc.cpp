/*
 * Copyright © 2022 Collabora, Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <cstdio>
#include <ctime>
#include <algorithm>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>

#include "shell.h"
#include "log.h"
#include "main-grpc.h"
#include "grpc-async-cb.h"

struct shell_data_init {
	struct agl_shell *shell;
	bool wait_for_bound;
	bool bound_ok;
	bool bound_fail;
	int version;
};

static int running = 1;

static void
agl_shell_bound_ok_init(void *data, struct agl_shell *agl_shell)
{
	(void) agl_shell;

	struct shell_data_init *sh = static_cast<struct shell_data_init *>(data);
	sh->wait_for_bound = false;

	sh->bound_ok = true;
}

static void
agl_shell_bound_fail_init(void *data, struct agl_shell *agl_shell)
{
	(void) agl_shell;

	struct shell_data_init *sh = static_cast<struct shell_data_init *>(data);
	sh->wait_for_bound = false;

	sh->bound_fail = true;
}

static void
agl_shell_bound_ok(void *data, struct agl_shell *agl_shell)
{
	(void) agl_shell;

	struct shell_data *sh = static_cast<struct shell_data *>(data);
	sh->wait_for_bound = false;

	sh->bound_ok = true;
}

static void
agl_shell_bound_fail(void *data, struct agl_shell *agl_shell)
{
	(void) agl_shell;

	struct shell_data *sh = static_cast<struct shell_data *>(data);
	sh->wait_for_bound = false;

	sh->bound_ok = false;
}

static void
agl_shell_app_state(void *data, struct agl_shell *agl_shell,
		const char *app_id, uint32_t state)
{
	(void) agl_shell;
	struct shell_data *sh = static_cast<struct shell_data *>(data);
	LOG("got app_state event app_id %s,  state %d\n", app_id, state);

	if (sh->server_context_list.empty())
		return;

	::agl_shell_ipc::AppStateResponse app;

	sh->current_app_state.set_app_id(std::string(app_id));
	sh->current_app_state.set_state(state);

	auto start = sh->server_context_list.begin();
	while (start != sh->server_context_list.end()) {
		// hold on if we're still detecting another in-flight writting
		if (start->second->Writting()) {
			LOG("skip writing to lister %p\n", static_cast<void *>(start->second));
			continue;
		}

		LOG("writing to lister %p\n", static_cast<void *>(start->second));
		start->second->NextWrite();
		start++;
	}
}

static const struct agl_shell_listener shell_listener = {
        agl_shell_bound_ok,
        agl_shell_bound_fail,
        agl_shell_app_state,
};

static const struct agl_shell_listener shell_listener_init = {
        agl_shell_bound_ok_init,
        agl_shell_bound_fail_init,
        nullptr,
};

static void
agl_shell_ext_doas_done(void *data, struct agl_shell_ext *agl_shell_ext, uint32_t status)
{
	(void) agl_shell_ext;

	struct shell_data *sh = static_cast<struct shell_data *>(data);
	sh->wait_for_doas = false;

	if (status == AGL_SHELL_EXT_DOAS_SHELL_CLIENT_STATUS_SUCCESS)
		sh->doas_ok = true;
}

static const struct agl_shell_ext_listener shell_ext_listener = {
        agl_shell_ext_doas_done,
};

static void
display_handle_geometry(void *data, struct wl_output *wl_output,
		int x, int y, int physical_width, int physical_height,
		int subpixel, const char *make, const char *model, int transform)
{
	(void) data;
	(void) wl_output;
	(void) x;
	(void) y;
	(void) physical_width;
	(void) physical_height;
	(void) subpixel;
	(void) make;
	(void) model;
	(void) transform;
}

static void
display_handle_mode(void *data, struct wl_output *wl_output, uint32_t flags,
		int width, int height, int refresh)
{
	(void) data;
	(void) wl_output;
	(void) flags;
	(void) width;
	(void) height;
	(void) refresh;
}

static void
display_handle_done(void *data, struct wl_output *wl_output)
{
	(void) data;
	(void) wl_output;
}

static void
display_handle_scale(void *data, struct wl_output *wl_output, int32_t factor)
{
	(void) data;
	(void) wl_output;
	(void) factor;
}


static void
display_handle_name(void *data, struct wl_output *wl_output, const char *name)
{
	(void) wl_output;

	struct window_output *woutput = static_cast<struct window_output *>(data);
	woutput->name = strdup(name);
}

static void
display_handle_description(void *data, struct wl_output *wl_output, const char *description)
{
	(void) data;
	(void) wl_output;
	(void) description;
}

static const struct wl_output_listener output_listener = {
	display_handle_geometry,
	display_handle_mode,
	display_handle_done,
	display_handle_scale,
	display_handle_name,
	display_handle_description,
};

static void
display_add_output(struct shell_data *sh, struct wl_registry *reg,
		   uint32_t id, uint32_t version)
{
	struct window_output *w_output;

	w_output = new struct window_output;
	w_output->shell_data = sh;

	w_output->output =
		static_cast<struct wl_output *>(wl_registry_bind(reg, id,
				&wl_output_interface,
				std::min(version, static_cast<uint32_t>(4))));

	wl_list_insert(&sh->output_list, &w_output->link);
	wl_output_add_listener(w_output->output, &output_listener, w_output);
}

static void
destroy_output(struct window_output *w_output)
{
	free(w_output->name);
	wl_list_remove(&w_output->link);
	free(w_output);
}

static void
global_add(void *data, struct wl_registry *reg, uint32_t id,
		const char *interface, uint32_t version)
{

	struct shell_data *sh = static_cast<struct shell_data *>(data);

	if (!sh)
		return;

	if (strcmp(interface, agl_shell_interface.name) == 0) {
		// bind to at least v3 to get events
		sh->shell =
			static_cast<struct agl_shell *>(wl_registry_bind(reg, id,
				&agl_shell_interface,
				std::min(static_cast<uint32_t>(3), version)));
		agl_shell_add_listener(sh->shell, &shell_listener, data);
		sh->version = version;
	} else if (strcmp(interface, "wl_output") == 0) {
                display_add_output(sh, reg, id, version);
        }
}

// the purpose of this _init is to make sure we're not the first shell client
// running to allow the 'main' shell client take over.
static void
global_add_init(void *data, struct wl_registry *reg, uint32_t id,
		const char *interface, uint32_t version)
{

	struct shell_data_init *sh = static_cast<struct shell_data_init *>(data);

	if (!sh)
		return;

	if (strcmp(interface, agl_shell_interface.name) == 0) {
		sh->shell =
			static_cast<struct agl_shell *>(wl_registry_bind(reg, id,
				&agl_shell_interface,
				std::min(static_cast<uint32_t>(3), version)));
		agl_shell_add_listener(sh->shell, &shell_listener_init, data);
		sh->version = version;
	}
}

static void
global_remove(void *data, struct wl_registry *reg, uint32_t id)
{
	/* Don't care */
	(void) data;
	(void) reg;
	(void) id;
}

static void
global_add_ext(void *data, struct wl_registry *reg, uint32_t id,
		const char *interface, uint32_t version)
{
	struct shell_data *sh = static_cast<struct shell_data *>(data);

	if (!sh)
		return;

	if (strcmp(interface, agl_shell_ext_interface.name) == 0) {
		sh->shell_ext =
			static_cast<struct agl_shell_ext *>(wl_registry_bind(reg, id,
				&agl_shell_ext_interface,
				std::min(static_cast<uint32_t>(1), version)));
		agl_shell_ext_add_listener(sh->shell_ext,
					   &shell_ext_listener, data);
	}
}

static const struct wl_registry_listener registry_ext_listener = {
	global_add_ext,
	global_remove,
};

static const struct wl_registry_listener registry_listener = {
	global_add,
	global_remove,
};

static const struct wl_registry_listener registry_listener_init = {
	global_add_init,
	global_remove,
};

static void
register_shell_ext(struct wl_display *wl_display, struct shell_data *sh)
{
	struct wl_registry *registry;

	registry = wl_display_get_registry(wl_display);

	wl_registry_add_listener(registry, &registry_ext_listener, sh);

	wl_display_roundtrip(wl_display);
	wl_registry_destroy(registry);
}

static void
register_shell(struct wl_display *wl_display, struct shell_data *sh)
{
	struct wl_registry *registry;

	wl_list_init(&sh->output_list);

	registry = wl_display_get_registry(wl_display);

	wl_registry_add_listener(registry, &registry_listener, sh);

	wl_display_roundtrip(wl_display);
	wl_registry_destroy(registry);
}

static int
__register_shell_init(void)
{
	int ret = 0;
	struct wl_registry *registry;
	struct wl_display *wl_display;

	struct shell_data_init *sh = new struct shell_data_init;

	wl_display = wl_display_connect(NULL);
	registry = wl_display_get_registry(wl_display);
	sh->wait_for_bound = true;
	sh->bound_fail = false;
	sh->bound_ok = false;

	wl_registry_add_listener(registry, &registry_listener_init, sh);
	wl_display_roundtrip(wl_display);

	if (!sh->shell || sh->version < 3) {
		ret = -1;
		goto err;
	}

	while (ret !=- 1 && sh->wait_for_bound) {
		ret = wl_display_dispatch(wl_display);

		if (sh->wait_for_bound)
			continue;
	}

	ret = sh->bound_fail;

	agl_shell_destroy(sh->shell);
	wl_display_flush(wl_display);
err:
	wl_registry_destroy(registry);
	wl_display_disconnect(wl_display);
	delete sh;
	return ret;
}

// we expect this client to be up & running *after* the shell client has
// already set-up panels/backgrounds.
// this means the very first try to bind to agl_shell we wait for
// 'bound_fail' event, which would tell us when it's ok to attempt to
// bind agl_shell_ext, call doas request, then attempt to bind (one
// more time) to agl_shell but this time wait for 'bound_ok' event.
void
register_shell_init(void)
{
	struct timespec ts = {};

	clock_gettime(CLOCK_MONOTONIC, &ts);

	ts.tv_sec = 0;
	ts.tv_nsec = 250 * 1000 * 1000;	// 250 ms

	// verify if 'bound_fail' was received
	while (true) {

		int r = __register_shell_init();

		if (r < 0) {
			LOG("agl-shell extension not found or version too low\n");
			exit(EXIT_FAILURE);
		} else if (r == 1) {
			// we need to get a 'bound_fail' event, if we get a 'bound_ok'
			// it means we're the first shell to start so wait until the
			// shell client actually started
			LOG("Found another shell client running. "
			     "Going further to bind to the agl_shell_ext interface\n");
			break;
		}

		LOG("No shell client detected running. Will wait until one starts up...\n");
		nanosleep(&ts, NULL);
	}

}

static void
destroy_shell_data(struct shell_data *sh)
{
        struct window_output *w_output, *w_output_next;

        wl_list_for_each_safe(w_output, w_output_next, &sh->output_list, link)
                destroy_output(w_output);

        wl_display_flush(sh->wl_display);
        wl_display_disconnect(sh->wl_display);

	delete sh;
}

static struct shell_data *
start_agl_shell_client(void)
{
	int ret = 0;
	struct wl_display *wl_display;

	wl_display = wl_display_connect(NULL);

	struct shell_data *sh = new struct shell_data;

	sh->wl_display = wl_display;
	sh->wait_for_doas = true;
	sh->wait_for_bound = true;

	register_shell_ext(wl_display, sh);

	// check for agl_shell_ext
	if (!sh->shell_ext) {
		LOG("Failed to bind to agl_shell_ext interface\n");
		goto err;
	}

	if (wl_list_empty(&sh->output_list)) {
		LOG("Failed get any outputs!\n");
		goto err;
	}

	agl_shell_ext_doas_shell_client(sh->shell_ext);
	while (ret != -1 && sh->wait_for_doas) {
		ret = wl_display_dispatch(sh->wl_display);
		if (sh->wait_for_doas)
			continue;
	}

	if (!sh->doas_ok) {
		LOG("Failed to get doas_done event\n");
		goto err;
	}

	// bind to agl-shell
	register_shell(wl_display, sh);
	while (ret != -1 && sh->wait_for_bound) {
		ret = wl_display_dispatch(sh->wl_display);
		if (sh->wait_for_bound)
			continue;
	}

	// at this point, we can't do anything about it
	if (!sh->bound_ok) {
		LOG("Failed to get bound_ok event!\n");
		goto err;
	}

	LOG("agl_shell/agl_shell_ext interface OK\n");

	return sh;
err:
	delete sh;
	return nullptr;
}

static void
start_grpc_server(Shell *aglShell)
{
	// instantiante the grpc server
	std::string server_address(kDefaultGrpcServiceAddress);
	GrpcServiceImpl service{aglShell};

	grpc::EnableDefaultHealthCheckService(true);
	grpc::reflection::InitProtoReflectionServerBuilderPlugin();

	grpc::ServerBuilder builder;
	builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
	builder.RegisterService(&service);

	std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
	LOG("gRPC server listening on %s\n", server_address.c_str());

	server->Wait();
}

int main(int argc, char **argv)
{
	(void) argc;
	(void) argv;
	Shell *aglShell;
	int ret = 0;

	// this blocks until we detect that another shell client started
	// running
	register_shell_init();

	struct shell_data *sh = start_agl_shell_client();
	if (!sh) {
		LOG("Failed to initialize agl-shell/agl-shell-ext\n");
		exit(EXIT_FAILURE);
	}

	std::shared_ptr<struct agl_shell> agl_shell{sh->shell, agl_shell_destroy};
	aglShell = new Shell(agl_shell, sh);

	std::thread thread(start_grpc_server, aglShell);

	// serve wayland requests
	while (running && ret != -1) {
		ret = wl_display_dispatch(sh->wl_display);
	}

	destroy_shell_data(sh);
	return 0;
}
