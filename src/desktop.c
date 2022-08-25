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

#include "shared/helpers.h"
#include <libweston/libweston.h>
#include <libweston-desktop/libweston-desktop.h>

#include "agl-shell-desktop-server-protocol.h"

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

struct weston_output *
get_default_output(struct weston_compositor *compositor)
{
	if (wl_list_empty(&compositor->output_list))
		return NULL;

	return container_of(compositor->output_list.next,
			struct weston_output, link);
}

struct weston_output *
get_focused_output(struct weston_compositor *compositor)
{
	struct weston_seat *seat;
	struct weston_output *output = NULL;

	wl_list_for_each(seat, &compositor->seat_list, link) {
		struct weston_touch *touch = weston_seat_get_touch(seat);
		struct weston_pointer *pointer = weston_seat_get_pointer(seat);
		struct weston_keyboard *keyboard =
			weston_seat_get_keyboard(seat);

		if (touch && touch->focus)
			output = touch->focus->output;
		else if (pointer && pointer->focus)
			output = pointer->focus->output;
		else if (keyboard && keyboard->focus)
			output = keyboard->focus->output;

		if (output)
			break;
	}

	return output;
}

void
ivi_shell_activate_surface(struct ivi_surface *ivi_surf,
                          struct ivi_shell_seat *ivi_seat,
                          uint32_t flags)
{
       struct weston_desktop_surface *dsurface = ivi_surf->dsurface;
       struct weston_surface *surface =
               weston_desktop_surface_get_surface(dsurface);

       weston_view_activate_input(ivi_surf->view, ivi_seat->seat, flags);

       if (ivi_seat->focused_surface) {
               struct ivi_surface *current_focus =
                       get_ivi_shell_surface(ivi_seat->focused_surface);
               struct weston_desktop_surface *dsurface_focus;
               assert(current_focus);

               dsurface_focus = current_focus->dsurface;
               if (--current_focus->focus_count == 0)
                       weston_desktop_surface_set_activated(dsurface_focus, false);
       }

       ivi_seat->focused_surface = surface;
       if (ivi_surf->focus_count++ == 0)
               weston_desktop_surface_set_activated(dsurface, true);
}


static void
desktop_surface_added_configure(struct ivi_surface *surface,
				struct ivi_output *ivi_output)
{
	enum ivi_surface_role role = IVI_SURFACE_ROLE_NONE;
	struct weston_desktop_surface *dsurface = surface->dsurface;

	ivi_check_pending_surface_desktop(surface, &role);
	if ((role != IVI_SURFACE_ROLE_DESKTOP &&
	     role != IVI_SURFACE_ROLE_FULLSCREEN &&
	     role != IVI_SURFACE_ROLE_REMOTE) ||
	     role == IVI_SURFACE_ROLE_NONE)
		return;

	if (role == IVI_SURFACE_ROLE_FULLSCREEN) {
		struct ivi_output *bg_output =
			ivi_layout_find_bg_output(surface->ivi);
		assert(bg_output);
		weston_desktop_surface_set_fullscreen(dsurface, true);
		weston_desktop_surface_set_size(dsurface,
						bg_output->output->width,
						bg_output->output->height);
		return;
	}

	weston_desktop_surface_set_maximized(dsurface, true);
	weston_desktop_surface_set_size(dsurface,
					ivi_output->area.width,
					ivi_output->area.height);
}


static void
desktop_surface_added(struct weston_desktop_surface *dsurface, void *userdata)
{
	struct ivi_compositor *ivi = userdata;
	struct weston_desktop_client *dclient;
	struct wl_client *client;
	struct ivi_surface *surface;
	struct ivi_output *active_output = NULL;
	struct weston_output *output = NULL;
	const char *app_id = NULL;

	dclient = weston_desktop_surface_get_client(dsurface);
	client = weston_desktop_client_get_client(dclient);

	if (ivi->shell_client.resource &&
	    ivi->shell_client.status == BOUND_FAILED) {
		wl_client_post_implementation_error(client,
				       "agl_shell has already been bound. "
				       "Check out bound_fail event");
		return;
	}

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
	surface->mapped = false;
	surface->advertised_on_launch = false;
	surface->checked_pending = false;
	wl_list_init(&surface->link);

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

	output =  get_focused_output(ivi->compositor);
	if (!output)
		output = get_default_output(ivi->compositor);

	if (output && ivi->shell_client.ready) {
		struct ivi_output *ivi_output = to_ivi_output(output);
		if (active_output)
			desktop_surface_added_configure(surface, active_output);
		else
			desktop_surface_added_configure(surface, ivi_output);
	}
	/*
	 * We delay creating "normal" desktop surfaces until later, to
	 * give the shell-client an oppurtunity to set the surface as a
	 * background/panel.
	 * Also delay the creation in order to have a valid app_id
	 * which will be used to set the proper role.
	 */
	weston_log("Added surface %p, app_id %s to pending list\n",
			surface, app_id);
	wl_list_insert(&ivi->pending_surfaces, &surface->link);

}

static bool
desktop_surface_check_last_surfaces(struct ivi_output *ivi_output, enum ivi_surface_role role)
{
	int count = 0;
	struct ivi_surface *surf;

	wl_list_for_each(surf, &ivi_output->ivi->surfaces, link)
		if (surf->role == role &&
		    surf->current_completed_output == ivi_output)
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
	const char *app_id = NULL;
	struct weston_seat *wseat = NULL;
	struct ivi_shell_seat *ivi_seat = NULL;
	struct ivi_output *output = NULL;

	/* we might not have a valid ivi_surface if _added failed due to
	 * protocol errors */
	if (!surface)
		return;

	wseat = get_ivi_shell_weston_first_seat(surface->ivi);
	if (wseat)
		ivi_seat = get_ivi_shell_seat(wseat);

	output = ivi_layout_get_output_from_surface(surface);

	wl_list_remove(&surface->listener_advertise_app.link);
	surface->listener_advertise_app.notify = NULL;

	app_id = weston_desktop_surface_get_app_id(dsurface);

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

	/* clear out focused_surface to avoid a stale focused_surface. the
	 * client shell is responsible for keeping track and switch back to the
	 * last active surface so we don't get do anything at removal, just
	 * reset it */
	if (ivi_seat && ivi_seat->focused_surface == wsurface)
		ivi_seat->focused_surface = NULL;

	if (surface->role == IVI_SURFACE_ROLE_REMOTE &&
	    output->type == OUTPUT_REMOTE)
		ivi_destroy_waltham_destroy(surface);

	/* check if there's a last 'remote' surface and insert a black
	 * surface view if there's no background set for that output
	 */
	if (desktop_surface_check_last_surfaces(output, IVI_SURFACE_ROLE_REMOTE) ||
	    desktop_surface_check_last_surfaces(output, IVI_SURFACE_ROLE_DESKTOP))
		if (!output->background)
			insert_black_curtain(output);


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
			app_id, ivi_layout_get_surface_role_name(surface));

	if (app_id && output)
		shell_advertise_app_state(output->ivi, app_id,
					  NULL, AGL_SHELL_DESKTOP_APP_STATE_DESTROYED);

	wl_list_remove(&surface->link);

	free(surface);
}

static void
desktop_committed(struct weston_desktop_surface *dsurface, 
		  int32_t sx, int32_t sy, void *userdata)
{
	struct ivi_compositor *ivi = userdata;
	struct ivi_surface *surface =
		weston_desktop_surface_get_user_data(dsurface);
	struct ivi_policy *policy = surface->ivi->policy;

	if (policy && policy->api.surface_commited &&
	    !policy->api.surface_commited(surface, surface->ivi))
		return;

	if (ivi->shell_client.ready && !surface->checked_pending) {
		const char * app_id =	weston_desktop_surface_get_app_id(dsurface);
		weston_log("Checking pending surface %p, app_id %s\n", surface,
			app_id);
		wl_list_remove(&surface->link);
		wl_list_init(&surface->link);
		ivi_check_pending_desktop_surface(surface);
		surface->checked_pending = true;
	}

	if (!surface->advertised_on_launch &&
	    !wl_list_empty(&surface->ivi->desktop_clients))
		wl_signal_emit(&surface->signal_advertise_app, surface);

	/* this repaint schedule is needed to allow resizing to work with the
	 * help of the hidden layer:
	 *
	 * 1. add the view in the hidden layer and send out correct dimensions
	 * 2. clients changes its dimensions
	 * 3. client commits with the new dimensions
	 *
	 * For desktop and fullscreen, desktop_surface_added() sends the
	 * dimensions from the beginning so applications no need to resize, but
	 * if that weren't the case we still need this in.
	 */
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

static void
ivi_shell_destroy(struct wl_listener *listener, void *data)
{
	struct ivi_compositor *ivi = container_of(listener,
				struct ivi_compositor, destroy_listener);

	ivi_shell_finalize(ivi);
	ivi_compositor_destroy_pending_surfaces(ivi);

	weston_desktop_destroy(ivi->desktop);
	wl_list_remove(&listener->link);
}

int
ivi_desktop_init(struct ivi_compositor *ivi)
{
	ivi->desktop = weston_desktop_create(ivi->compositor, &desktop_api, ivi);
	if (!ivi->desktop) {
		weston_log("Failed to create desktop globals");
		return -1;
	}

	if (!weston_compositor_add_destroy_listener_once(ivi->compositor,
			&ivi->destroy_listener, ivi_shell_destroy)) {
		return -1;
	}

	return 0;
}
