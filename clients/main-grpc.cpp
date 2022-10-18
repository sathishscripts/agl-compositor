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

static int running = 1;

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

	::agl_shell_ipc::AppState app;

	sh->current_app_state.set_app_id(std::string(app_id));
	sh->current_app_state.set_state(state);

	auto start = sh->server_context_list.begin();
	while (start != sh->server_context_list.end()) {
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
		sh->shell =
			static_cast<struct agl_shell *>(wl_registry_bind(reg, id,
					&agl_shell_interface, std::min(static_cast<uint32_t>(3),
									version)));
		agl_shell_add_listener(sh->shell, &shell_listener, data);
		sh->version = version;
	} else if (strcmp(interface, "wl_output") == 0) {
                display_add_output(sh, reg, id, version);
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
					&agl_shell_ext_interface, std::min(static_cast<uint32_t>(1),
									   version)));
		agl_shell_ext_add_listener(sh->shell_ext,
					   &shell_ext_listener, data);
	}
}

static void
global_remove_ext(void *data, struct wl_registry *reg, uint32_t id)
{
	/* Don't care */
	(void) data;
	(void) reg;
	(void) id;
}

static const struct wl_registry_listener registry_ext_listener = {
	global_add_ext,
	global_remove_ext,
};

static const struct wl_registry_listener registry_listener = {
	global_add,
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
		fprintf(stderr, "Failed to bind to agl_shell_ext interface\n");
		delete sh;
		return nullptr;
	}

	if (wl_list_empty(&sh->output_list)) {
		fprintf(stderr, "Failed get any outputs!\n");
		delete sh;
		return nullptr;
	}

	agl_shell_ext_doas_shell_client(sh->shell_ext);
	while (ret != -1 && sh->wait_for_doas) {
		ret = wl_display_dispatch(sh->wl_display);
		if (sh->wait_for_doas)
			continue;
	}

	if (!sh->doas_ok) {
		fprintf(stderr, "Failed to get doas_done event\n");
		delete sh;
		return nullptr;
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
		fprintf(stderr, "Failed to get bound_ok event!\n");
		delete sh;
		return nullptr;
	}

	fprintf(stderr, "agl_shell/agl_shell_ext interface OK\n");

	return sh; 
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
	LOG("Server listening on %s\n", server_address.c_str());

	server->Wait();
}

int main(int argc, char **argv)
{
	(void) argc;
	(void) argv;
	Shell *aglShell;
	int ret = 0;

	// do not start right up, give shell client time to boot up
	struct timespec ts = {};

	clock_gettime(CLOCK_MONOTONIC, &ts);
	ts.tv_sec = 2;
	ts.tv_nsec = 0;

	nanosleep(&ts, NULL);

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
