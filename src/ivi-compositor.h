/*
 * Copyright © 2019 Collabora, Ltd.
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

#ifndef IVI_COMPOSITOR_H
#define IVI_COMPOSITOR_H

#include <stdbool.h>

#include <libweston-6/compositor-drm.h>
#include <libweston-6/compositor.h>
#include <libweston-6/windowed-output-api.h>
#include <libweston-6/libweston-desktop.h>

#include "agl-shell-server-protocol.h"

#define ARRAY_LENGTH(x) (sizeof(x) / sizeof((x)[0]))

struct ivi_compositor {
	struct weston_compositor *compositor;
	struct weston_config *config;

	struct wl_listener heads_changed;

	bool init_failed;

	/*
	 * Options parsed from command line arugments.
	 * Overrides what is found in the config file.
	 */
	struct {
		/* drm */
		bool use_current_mode;
		/* wayland/x11 */
		int width;
		int height;
		int scale;
	} cmdline;
	const struct weston_windowed_output_api *window_api;
	const struct weston_drm_output_api *drm_api;

	struct wl_global *agl_shell;
	struct {
		struct wl_client *client;
		struct wl_resource *resource;
		bool ready;
	} shell_client;

	struct wl_list outputs; /* ivi_output.link */
	struct wl_list surfaces; /* ivi_desktop_surface.link */

	struct weston_desktop *desktop;

	struct wl_list pending_surfaces;

	struct weston_layer background;
	struct weston_layer normal;
	struct weston_layer panel;
	struct weston_layer fullscreen;

	struct wl_list shell_clients; /* ivi_shell_client.link */
};

struct ivi_surface;

struct ivi_output {
	struct wl_list link; /* ivi_compositor.outputs */
	struct ivi_compositor *ivi;

	char *name;
	struct weston_config_section *config;
	struct weston_output *output;

	struct ivi_surface *background;
	/* Panels */
	struct ivi_surface *top;
	struct ivi_surface *bottom;
	struct ivi_surface *left;
	struct ivi_surface *right;

	struct wl_listener output_destroy;

	/*
	 * Usable area for normal clients, i.e. with panels removed.
	 * In output-coorrdinate space.
	 */
	struct weston_geometry area;

	//int32_t width;
	//int32_t height;

	/* Temporary: only used during configuration */
	size_t add_len;
	struct weston_head *add[8];
};

enum ivi_surface_role {
	IVI_SURFACE_ROLE_NONE,
	IVI_SURFACE_ROLE_DESKTOP,
	IVI_SURFACE_ROLE_BACKGROUND,
	IVI_SURFACE_ROLE_PANEL,
};

struct ivi_desktop_surface {
	struct weston_view *view;
};

struct ivi_background_surface {
	struct ivi_output *output;
	struct weston_view *view;
};

struct ivi_panel_surface {
	struct ivi_output *output;
	enum agl_shell_edge edge;
	struct weston_view *view;
};

enum ivi_surface_flags {
	IVI_SURFACE_PROP_MAP = (1 << 0),
	/* x, y, width, height */
	IVI_SURFACE_PROP_POSITION = (1 << 1),
};

struct ivi_surface {
	struct ivi_compositor *ivi;
	struct weston_desktop_surface *dsurface;

	struct wl_list link;

	struct {
		enum ivi_surface_flags flags;
		int32_t x, y;
		int32_t width, height;
	} pending;

	struct weston_geometry old_geom;

	enum ivi_surface_role role;
	union {
		struct ivi_desktop_surface desktop;
		struct ivi_background_surface bg;
		struct ivi_panel_surface panel;
	};
};

struct ivi_shell_client {
	struct wl_list link;
	char *command;
	bool require_ready;

	pid_t pid;
	struct wl_client *client;

	struct wl_listener client_destroy;
};

struct ivi_compositor *
to_ivi_compositor(struct weston_compositor *ec);

int
ivi_shell_init(struct ivi_compositor *ivi);

int
ivi_shell_create_global(struct ivi_compositor *ivi);

int
ivi_launch_shell_client(struct ivi_compositor *ivi);

int
ivi_desktop_init(struct ivi_compositor *ivi);

struct ivi_shell_client *
ivi_shell_client_from_wl(struct wl_client *client);

struct ivi_output *
to_ivi_output(struct weston_output *o);

void
ivi_set_desktop_surface(struct ivi_surface *surface);

void
ivi_reflow_outputs(struct ivi_compositor *ivi);

struct ivi_surface *
to_ivi_surface(struct weston_surface *surface);

void
ivi_layout_set_mapped(struct ivi_surface *surface);

void
ivi_layout_set_position(struct ivi_surface *surface,
			int32_t x, int32_t y,
			int32_t width, int32_t height);

void
ivi_layout_commit(struct ivi_compositor *ivi);

#endif
