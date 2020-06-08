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

#include <assert.h>
#include "ivi-compositor.h"
#include "policy.h"

#include <libweston/libweston.h>
#include <libweston-desktop/libweston-desktop.h>

#if 0
static struct weston_output *
get_default_output(struct weston_compositor *compositor)
{
	if (wl_list_empty(&compositor->output_list))
		return NULL;

	return wl_container_of(compositor->output_list.next,
			       struct weston_output, link);
}
#endif

static void
desktop_ping_timeout(struct weston_desktop_client *dclient, void *userdata)
{
	/* not supported */
}

static void
desktop_pong(struct weston_desktop_client *dclient, void *userdata)
{
	/* not supported */
}

static void
desktop_surface_added(struct weston_desktop_surface *dsurface, void *userdata)
{
	struct ivi_compositor *ivi = userdata;
	struct weston_desktop_client *dclient;
	struct wl_client *client;
	struct ivi_surface *surface;

	dclient = weston_desktop_surface_get_client(dsurface);
	client = weston_desktop_client_get_client(dclient);

	surface = zalloc(sizeof *surface);
	if (!surface) {
		wl_client_post_no_memory(client);
		return;
	}

	surface->view = weston_desktop_surface_create_view(dsurface);
	if (!surface->view) {
		free(surface);
		wl_client_post_no_memory(client);
		return;
	}

	surface->ivi = ivi;
	surface->dsurface = dsurface;
	surface->role = IVI_SURFACE_ROLE_NONE;
	surface->activated_by_default = false;

	if (ivi->policy && ivi->policy->api.surface_create &&
	    !ivi->policy->api.surface_create(surface, ivi)) {
		free(surface);
		wl_client_post_no_memory(client);
		return;
	}

	weston_desktop_surface_set_user_data(dsurface, surface);

	if (ivi->shell_client.ready) {
		if (ivi_check_pending_desktop_surface_popup(surface))
			ivi_set_desktop_surface_popup(surface);
		else
			ivi_set_desktop_surface(surface);
	} else {
		/*
		 * We delay creating "normal" desktop surfaces until later, to
		 * give the shell-client an oppurtunity to set the surface as a
		 * background/panel.
		 */
		wl_list_insert(&ivi->pending_surfaces, &surface->link);
	}
}

static void
desktop_surface_removed(struct weston_desktop_surface *dsurface, void *userdata)
{
	struct ivi_surface *surface =
		weston_desktop_surface_get_user_data(dsurface);
	struct weston_surface *wsurface =
		weston_desktop_surface_get_surface(dsurface);

	struct ivi_output *output;

	if (surface->role == IVI_SURFACE_ROLE_DESKTOP)
		output = surface->desktop.last_output;
	else if (surface->role == IVI_SURFACE_ROLE_POPUP)
		output = surface->popup.output;
	else
		return;

	/* reset the active surface as well */
	if (output && output->active && output->active == surface) {
		output->active->view->is_mapped = false;
		output->active->view->surface->is_mapped = false;

		weston_layer_entry_remove(&output->active->view->layer_link);
		output->active = NULL;
	}
	if (weston_surface_is_mapped(wsurface)) {
		weston_desktop_surface_unlink_view(surface->view);
		weston_view_destroy(surface->view);
	}

	/* invalidate agl-shell surfaces so we can re-use them when
	 * binding again */
	if (surface->role == IVI_SURFACE_ROLE_PANEL) {
		switch (surface->panel.edge) {
		case AGL_SHELL_EDGE_TOP:
			output->top = NULL;
			break;
		case AGL_SHELL_EDGE_BOTTOM:
			output->bottom = NULL;
			break;
		case AGL_SHELL_EDGE_LEFT:
			output->left = NULL;
			break;
		case AGL_SHELL_EDGE_RIGHT:
			output->right = NULL;
			break;
		default:
			assert(!"Invalid edge detected\n");
		}
	} else if (surface->role == IVI_SURFACE_ROLE_BACKGROUND) {
		output->background = NULL;
	}

	wl_list_remove(&surface->link);
	free(surface);
}

static void
desktop_committed(struct weston_desktop_surface *dsurface, 
		  int32_t sx, int32_t sy, void *userdata)
{
	struct ivi_surface *surface =
		weston_desktop_surface_get_user_data(dsurface);
	struct ivi_policy *policy = surface->ivi->policy;

	if (policy && policy->api.surface_commited &&
	    !policy->api.surface_commited(surface, surface->ivi))
		return;

	weston_compositor_schedule_repaint(surface->ivi->compositor);

	switch (surface->role) {
	case IVI_SURFACE_ROLE_DESKTOP:
		ivi_layout_desktop_committed(surface);
		break;
	case IVI_SURFACE_ROLE_PANEL:
		ivi_layout_panel_committed(surface);
		break;
	case IVI_SURFACE_ROLE_POPUP:
		ivi_layout_popup_committed(surface);
		break;
	case IVI_SURFACE_ROLE_NONE:
	case IVI_SURFACE_ROLE_BACKGROUND:
	default: /* fall through */
		break;
	}
}

static void
desktop_show_window_menu(struct weston_desktop_surface *dsurface,
			 struct weston_seat *seat, int32_t x, int32_t y,
			 void *userdata)
{
	/* not supported */
}

static void
desktop_set_parent(struct weston_desktop_surface *dsurface,
		   struct weston_desktop_surface *parent, void *userdata)
{
	/* not supported */
}

static void
desktop_move(struct weston_desktop_surface *dsurface,
	     struct weston_seat *seat, uint32_t serial, void *userdata)
{
	/* not supported */
}

static void
desktop_resize(struct weston_desktop_surface *dsurface,
	       struct weston_seat *seat, uint32_t serial,
	       enum weston_desktop_surface_edge edges, void *user_data)
{
	/* not supported */
}

static void
desktop_fullscreen_requested(struct weston_desktop_surface *dsurface,
			     bool fullscreen, struct weston_output *output,
			     void *userdata)
{
	/* not supported */
}

static void
desktop_maximized_requested(struct weston_desktop_surface *dsurface,
			    bool maximized, void *userdata)
{
	/* not supported */
}

static void
desktop_minimized_requested(struct weston_desktop_surface *dsurface,
			    void *userdata)
{
	/* not supported */
}

static void
desktop_set_xwayland_position(struct weston_desktop_surface *dsurface,
			      int32_t x, int32_t y, void *userdata)
{
	/* not supported */
}

static const struct weston_desktop_api desktop_api = {
	.struct_size = sizeof desktop_api,
	.ping_timeout = desktop_ping_timeout,
	.pong = desktop_pong,
	.surface_added = desktop_surface_added,
	.surface_removed = desktop_surface_removed,
	.committed = desktop_committed,
	.show_window_menu = desktop_show_window_menu,
	.set_parent = desktop_set_parent,
	.move = desktop_move,
	.resize = desktop_resize,
	.fullscreen_requested = desktop_fullscreen_requested,
	.maximized_requested = desktop_maximized_requested,
	.minimized_requested = desktop_minimized_requested,
	.set_xwayland_position = desktop_set_xwayland_position,
};

int
ivi_desktop_init(struct ivi_compositor *ivi)
{
	ivi->desktop = weston_desktop_create(ivi->compositor, &desktop_api, ivi);
	if (!ivi->desktop) {
		weston_log("Failed to create desktop globals");
		return -1;
	}

	return 0;
}
