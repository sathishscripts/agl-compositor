#pragma once

#include <cstdio>
#include <algorithm>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <wayland-client.h>

#include "agl_shell.grpc.pb.h"

// forward declaration created in grpc-async-cb
class Lister;

struct shell_data {
	struct wl_display *wl_display;
	struct agl_shell *shell;
	struct agl_shell_ext *shell_ext;

	bool wait_for_bound;
	bool wait_for_doas;

	bool bound_ok;
	bool doas_ok;

	uint32_t version;
	struct wl_list output_list;     /** window_output::link */

	::agl_shell_ipc::AppState current_app_state;
	std::list<std::pair<grpc::CallbackServerContext*, Lister *> > server_context_list;
};

struct window_output {
	struct shell_data *shell_data;
	struct wl_output *output;
	char *name;
	struct wl_list link;    /** display::output_list */
};
