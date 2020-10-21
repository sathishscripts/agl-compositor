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
desktop_advertise_app(struct wl_listener *listener, void *data)
{
	struct ivi_surface *surface;

	surface = wl_container_of(listener, surface, listener_advertise_app);

	agl_shell_desktop_advertise_application_id(surface->ivi, surface);
}

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
	struct ivi_output *active_output = NULL;
	const char *app_id = NULL;

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
	surface->advertised_on_launch = false;

	wl_signal_init(&surface->signal_advertise_app);

	surface->listener_advertise_app.notify = desktop_advertise_app;
	wl_signal_add(&surface->signal_advertise_app,
		      &surface->listener_advertise_app);

	weston_desktop_surface_set_user_data(dsurface, surface);

	if (ivi->policy && ivi->policy->api.surface_create &&
	    !ivi->policy->api.surface_create(surface, ivi)) {
		wl_client_post_no_memory(client);
		return;
	}


	app_id = weston_desktop_surface_get_app_id(dsurface);

	if ((active_output = ivi_layout_find_with_app_id(app_id, ivi)))
		ivi_set_pending_desktop_surface_remote(active_output, app_id);

	/* reset any caps to make sure we apply the new caps */
	ivi_seat_reset_caps_sent(ivi);

	if (ivi->shell_client.ready) {
		ivi_check_pending_desktop_surface(surface);
		weston_log("Added surface %p, app_id %s, role %s\n", surface,
				app_id, ivi_layout_get_surface_role_name(surface));
	} else {
		/*
		 * We delay creating "normal" desktop surfaces until later, to
		 * give the shell-client an oppurtunity to set the surface as a
		 * background/panel.
		 */
		weston_log("Added surface %p, app_id %s to pending list\n",
				surface, app_id);
		wl_list_insert(&ivi->pending_surfaces, &surface->link);
	}
}

static bool
desktop_surface_check_last_remote_surfaces(struct ivi_compositor *ivi, enum ivi_surface_role role)
{
	int count = 0;
	struct ivi_surface *surf;

	wl_list_for_each(surf, &ivi->surfaces, link)
		if (surf->role == role)
			count++;

	return (count == 1);
}

static void
desktop_surface_removed(struct weston_desktop_surface *dsurface, void *userdata)
{
	struct ivi_surface *surface =
		weston_desktop_surface_get_user_data(dsurface);
	struct weston_surface *wsurface =
		weston_desktop_surface_get_surface(dsurface);

	struct ivi_output *output = ivi_layout_get_output_from_surface(surface);

	wl_list_remove(&surface->listener_advertise_app.link);
	surface->listener_advertise_app.notify = NULL;

	/* special corner-case, pending_surfaces which are never activated or
	 * being assigned an output might land here so just remove the surface;
	 *
	 * the DESKTOP role can happen here as well, because we can fall-back 
	 * to that when we try to determine the role type. Application that
	 * do not set the app_id will be land here, when destroyed */
	if (output == NULL && (surface->role == IVI_SURFACE_ROLE_NONE ||
			       surface->role == IVI_SURFACE_ROLE_DESKTOP))
		goto skip_output_asignment;

	assert(output != NULL);

	/* resize the active surface to the original size */
	if (surface->role == IVI_SURFACE_ROLE_SPLIT_H ||
	    surface->role == IVI_SURFACE_ROLE_SPLIT_V) {
		if (output && output->active) {
			ivi_layout_desktop_resize(output->active, output->area_saved);
		}
		/* restore the area back so we can re-use it again if needed */
		output->area = output->area_saved;
	}

	/* reset the active surface as well */
	if (output && output->active && output->active == surface) {
		output->active->view->is_mapped = false;
		output->active->view->surface->is_mapped = false;

		weston_layer_entry_remove(&output->active->view->layer_link);
		output->active = NULL;
	}

	/* check if there's a last 'remote' surface and insert a black
	 * surface view if there's no background set for that output
	 */
	if ((desktop_surface_check_last_remote_surfaces(output->ivi,
		IVI_SURFACE_ROLE_REMOTE) ||
	    desktop_surface_check_last_remote_surfaces(output->ivi,
		IVI_SURFACE_ROLE_DESKTOP)) && output->type == OUTPUT_REMOTE)
		if (!output->background)
			insert_black_surface(output);


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

skip_output_asignment:
	weston_log("Removed surface %p, app_id %s, role %s\n", surface,
			weston_desktop_surface_get_app_id(dsurface),
			ivi_layout_get_surface_role_name(surface));

	/* we weren't added to any list if we are still with 'none' as role */
	if (surface->role != IVI_SURFACE_ROLE_NONE)
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

	if (!surface->advertised_on_launch)
		wl_signal_emit(&surface->signal_advertise_app, surface);

	weston_compositor_schedule_repaint(surface->ivi->compositor);

	switch (surface->role) {
	case IVI_SURFACE_ROLE_DESKTOP:
	case IVI_SURFACE_ROLE_REMOTE:
		ivi_layout_desktop_committed(surface);
		break;
	case IVI_SURFACE_ROLE_POPUP:
		ivi_layout_popup_committed(surface);
		break;
	case IVI_SURFACE_ROLE_FULLSCREEN:
		ivi_layout_fullscreen_committed(surface);
		break;
	case IVI_SURFACE_ROLE_SPLIT_H:
	case IVI_SURFACE_ROLE_SPLIT_V:
		ivi_layout_split_committed(surface);
		break;
	case IVI_SURFACE_ROLE_NONE:
	case IVI_SURFACE_ROLE_BACKGROUND:
	case IVI_SURFACE_ROLE_PANEL:
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
