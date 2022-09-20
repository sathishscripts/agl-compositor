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

#include "ivi-compositor.h"
#include "policy.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <libweston/libweston.h>
#include <libweston/config-parser.h>

#include <weston/weston.h>
#include "shared/os-compatibility.h"
#include "shared/helpers.h"
#include "shared/process-util.h"

#include "agl-shell-server-protocol.h"
#include "agl-shell-desktop-server-protocol.h"

#ifdef HAVE_WALTHAM
#include <waltham-transmitter/transmitter_api.h>
#endif

static void
create_black_curtain_view(struct ivi_output *output);

void
agl_shell_desktop_advertise_application_id(struct ivi_compositor *ivi,
					   struct ivi_surface *surface)
{
	struct desktop_client *dclient;
	static bool display_adv = false;

	if (surface->advertised_on_launch)
		return;

	/* advertise to all desktop clients the new surface */
	wl_list_for_each(dclient, &ivi->desktop_clients, link) {
		const char *app_id =
			weston_desktop_surface_get_app_id(surface->dsurface);
		if (app_id == NULL) {
			if (!display_adv) {
				weston_log("WARNING app_is is null, unable to advertise\n");
				display_adv = true;
			}
			return;
		}
		agl_shell_desktop_send_application(dclient->resource, app_id);
		surface->advertised_on_launch = true;
	}
}

void
ivi_set_desktop_surface(struct ivi_surface *surface)
{
	struct ivi_compositor *ivi = surface->ivi;
	assert(surface->role == IVI_SURFACE_ROLE_NONE);

	surface->role = IVI_SURFACE_ROLE_DESKTOP;
	wl_list_insert(&surface->ivi->surfaces, &surface->link);

	agl_shell_desktop_advertise_application_id(ivi, surface);
}

static void
ivi_set_desktop_surface_popup(struct ivi_surface *surface)
{
	struct ivi_compositor *ivi = surface->ivi;
	assert(surface->role == IVI_SURFACE_ROLE_NONE);

	surface->role = IVI_SURFACE_ROLE_POPUP;
	wl_list_insert(&ivi->surfaces, &surface->link);

	agl_shell_desktop_advertise_application_id(ivi, surface);
}

static void
ivi_set_desktop_surface_fullscreen(struct ivi_surface *surface)
{
	struct ivi_compositor *ivi = surface->ivi;
	assert(surface->role == IVI_SURFACE_ROLE_NONE);

	surface->role = IVI_SURFACE_ROLE_FULLSCREEN;
	wl_list_insert(&ivi->surfaces, &surface->link);

	agl_shell_desktop_advertise_application_id(ivi, surface);
}

#ifdef HAVE_WALTHAM
void
ivi_destroy_waltham_destroy(struct ivi_surface *surface)
{
	struct ivi_compositor *ivi = surface->ivi;
	const struct weston_transmitter_api *api =
		ivi->waltham_transmitter_api;

	if (!api)
		return;

	if (surface->waltham_surface.transmitter_surface)
		api->surface_destroy(surface->waltham_surface.transmitter_surface);
}

static void
ivi_output_notify_waltham_plugin(struct ivi_surface *surface)
{
	struct ivi_compositor *ivi = surface->ivi;
	const struct weston_transmitter_api *api = ivi->waltham_transmitter_api;
	struct weston_transmitter *transmitter;
	struct weston_transmitter_remote *trans_remote;
	struct weston_surface *weston_surface;
	struct weston_output *woutput = surface->remote.output->output;
	const char *app_id;

	if (!api)
		return;

	transmitter = api->transmitter_get(ivi->compositor);
	if (!transmitter)
		return;

	trans_remote = api->get_transmitter_remote(woutput->name, transmitter);
	if (!trans_remote) {
		weston_log("Could not find a valie weston_transmitter_remote "
				"that matches the output %s\n", woutput->name);
		return;
	}

	app_id = weston_desktop_surface_get_app_id(surface->dsurface);
	weston_surface =
		weston_desktop_surface_get_surface(surface->dsurface);

	weston_log("Forwarding app_id %s to remote %s\n", app_id, woutput->name);

	/* this will have the effect of informing the remote side to create a
	 * surface with the name app_id. W/ xdg-shell the following happens:
	 *
	 * compositor (server):
	 * surface_push_to_remote():
	 * 	waltham-transmitter plug-in
	 * 		-> wthp_ivi_app_id_surface_create()
	 *
	 * client -- on the receiver side:
	 * 	-> wthp_ivi_app_id_surface_create()
	 * 		-> wth_receiver_weston_main()
	 * 			-> wl_compositor_create_surface()
	 * 			-> xdg_wm_base_get_xdg_surface
	 * 			-> xdg_toplevel_set_app_id()
	 * 			-> gst_init()
	 * 			-> gst_parse_launch()
	 *
	 * wth_receiver_weston_main() will be invoked from the handler of
	 * wthp_ivi_app_id_surface_create() and is responsible for setting-up
	 * the gstreamer pipeline as well.
	 */
	surface->waltham_surface.transmitter_surface =
	    api->surface_push_to_remote(weston_surface, app_id, trans_remote, NULL);
}

#else
void
ivi_destroy_waltham_destroy(struct ivi_surface *surface)
{
}
static void
ivi_output_notify_waltham_plugin(struct ivi_surface *surface)
{
}
#endif

static void
ivi_set_desktop_surface_remote(struct ivi_surface *surface)
{
	struct ivi_compositor *ivi = surface->ivi;
	struct weston_view *view;
	struct ivi_output *output = surface->remote.output;

	assert(surface->role == IVI_SURFACE_ROLE_NONE);

	/* remote type are the same as desktop just that client can tell
	 * the compositor to start on another output */
	surface->role = IVI_SURFACE_ROLE_REMOTE;

	/* if thew black surface view is mapped on the mean we need
	 * to remove it in order to start showing the 'remote' surface
	 * just being added */
	view = output->fullscreen_view.fs->view;
	if (view->is_mapped || view->surface->is_mapped)
		remove_black_curtain(output);

	if (output->type == OUTPUT_WALTHAM)
		ivi_output_notify_waltham_plugin(surface);

	wl_list_insert(&ivi->surfaces, &surface->link);
}


static void
ivi_set_desktop_surface_split(struct ivi_surface *surface)
{
	struct ivi_compositor *ivi = surface->ivi;
	assert(surface->role == IVI_SURFACE_ROLE_NONE);

	if (surface->split.orientation == AGL_SHELL_DESKTOP_APP_ROLE_SPLIT_VERTICAL)
		surface->role = IVI_SURFACE_ROLE_SPLIT_V;
	else
		surface->role = IVI_SURFACE_ROLE_SPLIT_H;

	wl_list_insert(&ivi->surfaces, &surface->link);

	agl_shell_desktop_advertise_application_id(ivi, surface);
}

static struct pending_popup *
ivi_ensure_popup(struct ivi_output *ioutput, int x, int y, int bx, int by,
		 int width, int height, const char *app_id)
{
	struct pending_popup *p_popup = zalloc(sizeof(*p_popup));
	size_t len_app_id = strlen(app_id);

	if (!p_popup)
		return NULL;
	p_popup->app_id = zalloc(sizeof(char) * (len_app_id + 1));
	if (!p_popup->app_id) {
		free(p_popup);
		return NULL;
	}
	memcpy(p_popup->app_id, app_id, len_app_id);
	p_popup->ioutput = ioutput;
	p_popup->x = x;
	p_popup->y = y;

	p_popup->bb.x = bx;
	p_popup->bb.y = by;
	p_popup->bb.width = width;
	p_popup->bb.height = height;

	return p_popup;
}

static void
ivi_update_popup(struct ivi_output *ioutput, int x, int y, int bx, int by,
		 int width, int height, const char *app_id, struct pending_popup *p_popup)
{
	size_t len_app_id = strlen(app_id);

	wl_list_remove(&p_popup->link);
	wl_list_init(&p_popup->link);

	memset(p_popup->app_id, 0, strlen(app_id) + 1);
	free(p_popup->app_id);

	p_popup->app_id = zalloc(sizeof(char) * (len_app_id + 1));
	if (!p_popup->app_id)
		return;
	memcpy(p_popup->app_id, app_id, len_app_id);

	p_popup->ioutput = ioutput;
	p_popup->x = x;
	p_popup->y = y;

	p_popup->bb.x = bx;
	p_popup->bb.y = by;
	p_popup->bb.width = width;
	p_popup->bb.height = height;
}

static struct pending_fullscreen *
ivi_ensure_fullscreen(struct ivi_output *ioutput, const char *app_id)
{
	struct pending_fullscreen *p_fullscreen = zalloc(sizeof(*p_fullscreen));
	size_t len_app_id = strlen(app_id);

	if (!p_fullscreen)
		return NULL;
	p_fullscreen->app_id = zalloc(sizeof(char) * (len_app_id + 1));
	if (!p_fullscreen->app_id) {
		free(p_fullscreen);
		return NULL;
	}
	memcpy(p_fullscreen->app_id, app_id, len_app_id);

	p_fullscreen->ioutput = ioutput;
	return p_fullscreen;
}

static void
ivi_update_fullscreen(struct ivi_output *ioutput, const char *app_id,
		      struct pending_fullscreen *p_fullscreen)
{
	size_t len_app_id = strlen(app_id);

	wl_list_remove(&p_fullscreen->link);
	wl_list_init(&p_fullscreen->link);

	memset(p_fullscreen->app_id, 0, strlen(app_id) + 1);
	free(p_fullscreen->app_id);

	p_fullscreen->app_id = zalloc(sizeof(char) * (len_app_id + 1));
	if (!p_fullscreen->app_id)
		return;
	memcpy(p_fullscreen->app_id, app_id, len_app_id);

	p_fullscreen->ioutput = ioutput;
}

static struct pending_remote *
ivi_ensure_remote(struct ivi_output *ioutput, const char *app_id)
{
	struct pending_remote *p_remote = zalloc(sizeof(*p_remote));
	size_t len_app_id = strlen(app_id);

	if (!p_remote)
		return NULL;
	p_remote->app_id = zalloc(sizeof(char) * (len_app_id + 1));
	if (!p_remote->app_id) {
		free(p_remote);
		return NULL;
	}
	memcpy(p_remote->app_id, app_id, len_app_id);

	p_remote->ioutput = ioutput;
	return p_remote;
}

static void
ivi_update_remote(struct ivi_output *ioutput, const char *app_id,
		      struct pending_remote *p_remote)
{
	size_t len_app_id = strlen(app_id);

	wl_list_remove(&p_remote->link);
	wl_list_init(&p_remote->link);

	memset(p_remote->app_id, 0, strlen(app_id) + 1);
	free(p_remote->app_id);

	p_remote->app_id = zalloc(sizeof(char) * (len_app_id + 1));
	if (!p_remote->app_id)
		return;
	memcpy(p_remote->app_id, app_id, len_app_id);

	p_remote->ioutput = ioutput;
}

static void
ivi_set_pending_desktop_surface_popup(struct ivi_output *ioutput, int x, int y, int bx,
				      int by, int width, int height, const char *app_id)
{
	struct ivi_compositor *ivi = ioutput->ivi;
	struct pending_popup *p_popup = NULL;
	struct pending_popup *popup;

	wl_list_for_each(popup, &ivi->popup_pending_apps, link)
		if (!strcmp(app_id, popup->app_id))
			p_popup = popup;

	if (!p_popup)
		p_popup = ivi_ensure_popup(ioutput, x, y, bx, by, width, height, app_id);
	else
		ivi_update_popup(ioutput, x, y, bx, by, width, height, app_id, p_popup);
	if (!p_popup)
		return;

	wl_list_insert(&ivi->popup_pending_apps, &p_popup->link);
}

static void
ivi_set_pending_desktop_surface_fullscreen(struct ivi_output *ioutput,
					   const char *app_id)
{
	struct ivi_compositor *ivi = ioutput->ivi;
	struct pending_fullscreen *p_fullscreen = NULL;
	struct pending_fullscreen *fullscreen;

	wl_list_for_each(fullscreen, &ivi->fullscreen_pending_apps, link)
		if (!strcmp(app_id, fullscreen->app_id))
			p_fullscreen = fullscreen;

	if (!p_fullscreen)
		p_fullscreen = ivi_ensure_fullscreen(ioutput, app_id);
	else
		ivi_update_fullscreen(ioutput, app_id, p_fullscreen);

	if (!p_fullscreen)
		return;
	wl_list_insert(&ivi->fullscreen_pending_apps, &p_fullscreen->link);
}

static void
ivi_set_pending_desktop_surface_split(struct ivi_output *ioutput,
				      const char *app_id, uint32_t orientation)
{
	struct ivi_compositor *ivi = ioutput->ivi;
	struct ivi_surface *surf;
	size_t len_app_id = strlen(app_id);
	struct pending_split *split;

	if (orientation != AGL_SHELL_DESKTOP_APP_ROLE_SPLIT_VERTICAL &&
	    orientation != AGL_SHELL_DESKTOP_APP_ROLE_SPLIT_HORIZONTAL)
		return;

	/* more than one is un-supported, do note we need to do
	 * conversion for surface roles instead of using the protocol ones */
	wl_list_for_each(surf, &ivi->surfaces, link)
		if (surf->role == IVI_SURFACE_ROLE_SPLIT_V ||
		    surf->role == IVI_SURFACE_ROLE_SPLIT_H)
			return;

	split = zalloc(sizeof(*split));
	if (!split)
		return;
	split->app_id = zalloc(sizeof(char) * (len_app_id + 1));
	if (!split->app_id) {
		free(split);
		return;
	}
	memcpy(split->app_id, app_id, len_app_id);

	split->ioutput = ioutput;
	split->orientation = orientation;

	wl_list_insert(&ivi->split_pending_apps, &split->link);
}

void
ivi_set_pending_desktop_surface_remote(struct ivi_output *ioutput,
		const char *app_id)
{
	struct ivi_compositor *ivi = ioutput->ivi;
	struct pending_remote *remote;
	struct pending_remote *p_remote = NULL;

	wl_list_for_each(remote, &ivi->remote_pending_apps, link)
		if (!strcmp(app_id, remote->app_id))
			p_remote = remote;

	if (!p_remote)
		p_remote = ivi_ensure_remote(ioutput, app_id);
	else
		ivi_update_remote(ioutput, app_id, p_remote);
	if (!p_remote)
		return;

	wl_list_insert(&ivi->remote_pending_apps, &p_remote->link);
}


static void
ivi_remove_pending_desktop_surface_split(struct pending_split *split)
{
	free(split->app_id);
	wl_list_remove(&split->link);
	free(split);
}

static void
ivi_remove_pending_desktop_surface_fullscreen(struct pending_fullscreen *fs)
{
	free(fs->app_id);
	wl_list_remove(&fs->link);
	free(fs);
}

static void
ivi_remove_pending_desktop_surface_popup(struct pending_popup *p_popup)
{
	free(p_popup->app_id);
	wl_list_remove(&p_popup->link);
	free(p_popup);
}

static void
ivi_remove_pending_desktop_surface_remote(struct pending_remote *remote)
{
	free(remote->app_id);
	wl_list_remove(&remote->link);
	free(remote);
}

static bool
ivi_compositor_keep_pending_surfaces(struct ivi_surface *surface)
{
	return surface->ivi->keep_pending_surfaces;
}

static bool
ivi_check_pending_desktop_surface_popup(struct ivi_surface *surface)
{
	struct ivi_compositor *ivi = surface->ivi;
	struct pending_popup *p_popup, *next_p_popup;
	const char *_app_id =
			weston_desktop_surface_get_app_id(surface->dsurface);

	if (wl_list_empty(&ivi->popup_pending_apps) || !_app_id)
		return false;

	wl_list_for_each_safe(p_popup, next_p_popup,
			      &ivi->popup_pending_apps, link) {
		if (!strcmp(_app_id, p_popup->app_id)) {
			surface->popup.output = p_popup->ioutput;
			surface->popup.x = p_popup->x;
			surface->popup.y = p_popup->y;

			surface->popup.bb.x = p_popup->bb.x;
			surface->popup.bb.y = p_popup->bb.y;
			surface->popup.bb.width = p_popup->bb.width;
			surface->popup.bb.height = p_popup->bb.height;

			if (!ivi_compositor_keep_pending_surfaces(surface))
				ivi_remove_pending_desktop_surface_popup(p_popup);
			return true;
		}
	}

	return false;
}

static bool
ivi_check_pending_desktop_surface_split(struct ivi_surface *surface)
{
	struct pending_split *split_surf, *next_split_surf;
	struct ivi_compositor *ivi = surface->ivi;
	const char *_app_id =
			weston_desktop_surface_get_app_id(surface->dsurface);

	if (wl_list_empty(&ivi->split_pending_apps) || !_app_id)
		return false;

	wl_list_for_each_safe(split_surf, next_split_surf,
			      &ivi->split_pending_apps, link) {
		if (!strcmp(_app_id, split_surf->app_id)) {
			surface->split.output = split_surf->ioutput;
			surface->split.orientation = split_surf->orientation;
			if (!ivi_compositor_keep_pending_surfaces(surface))
				ivi_remove_pending_desktop_surface_split(split_surf);
			return true;
		}
	}

	return false;
}

static bool
ivi_check_pending_desktop_surface_fullscreen(struct ivi_surface *surface)
{
	struct pending_fullscreen *fs_surf, *next_fs_surf;
	struct ivi_compositor *ivi = surface->ivi;
	const char *_app_id =
			weston_desktop_surface_get_app_id(surface->dsurface);

	if (wl_list_empty(&ivi->fullscreen_pending_apps) || !_app_id)
		return false;

	wl_list_for_each_safe(fs_surf, next_fs_surf,
			      &ivi->fullscreen_pending_apps, link) {
		if (!strcmp(_app_id, fs_surf->app_id)) {
			surface->fullscreen.output = fs_surf->ioutput;
			if (!ivi_compositor_keep_pending_surfaces(surface))
				ivi_remove_pending_desktop_surface_fullscreen(fs_surf);
			return true;
		}
	}

	return false;
}

static bool
ivi_check_pending_desktop_surface_remote(struct ivi_surface *surface)
{
	struct pending_remote *remote_surf, *next_remote_surf;
	struct ivi_compositor *ivi = surface->ivi;
	const char *_app_id =
		weston_desktop_surface_get_app_id(surface->dsurface);

	if (wl_list_empty(&ivi->remote_pending_apps) || !_app_id)
		return false;

	wl_list_for_each_safe(remote_surf, next_remote_surf,
			      &ivi->remote_pending_apps, link) {
		if (!strcmp(_app_id, remote_surf->app_id)) {
			surface->remote.output = remote_surf->ioutput;
			if (!ivi_compositor_keep_pending_surfaces(surface))
				ivi_remove_pending_desktop_surface_remote(remote_surf);
			return true;
		}
	}

	return false;
}
void
ivi_check_pending_surface_desktop(struct ivi_surface *surface,
				  enum ivi_surface_role *role)
{
	struct ivi_compositor *ivi = surface->ivi;
	struct wl_list *role_pending_list;
	struct pending_popup *p_popup;
	struct pending_split *p_split;
	struct pending_fullscreen *p_fullscreen;
	struct pending_remote *p_remote;
	const char *app_id =
		weston_desktop_surface_get_app_id(surface->dsurface);

	role_pending_list = &ivi->popup_pending_apps;
	wl_list_for_each(p_popup, role_pending_list, link) {
		if (app_id && !strcmp(app_id, p_popup->app_id)) {
			*role = IVI_SURFACE_ROLE_POPUP;
			return;
		}
	}

	role_pending_list = &ivi->split_pending_apps;
	wl_list_for_each(p_split, role_pending_list, link) {
		if (app_id && !strcmp(app_id, p_split->app_id)) {
			*role = IVI_SURFACE_ROLE_SPLIT_V;
			return;
		}
	}

	role_pending_list = &ivi->fullscreen_pending_apps;
	wl_list_for_each(p_fullscreen, role_pending_list, link) {
		if (app_id && !strcmp(app_id, p_fullscreen->app_id)) {
			*role = IVI_SURFACE_ROLE_FULLSCREEN;
			return;
		}
	}

	role_pending_list = &ivi->remote_pending_apps;
	wl_list_for_each(p_remote, role_pending_list, link) {
		if (app_id && !strcmp(app_id, p_remote->app_id)) {
			*role = IVI_SURFACE_ROLE_REMOTE;
			return;
		}
	}

	/* else, we are a regular desktop surface */
	*role = IVI_SURFACE_ROLE_DESKTOP;
}


void
ivi_check_pending_desktop_surface(struct ivi_surface *surface)
{
	bool ret = false;

	ret = ivi_check_pending_desktop_surface_popup(surface);
	if (ret) {
		ivi_set_desktop_surface_popup(surface);
		ivi_layout_popup_committed(surface);
		return;
	}

	ret = ivi_check_pending_desktop_surface_split(surface);
	if (ret) {
		ivi_set_desktop_surface_split(surface);
		ivi_layout_split_committed(surface);
		return;
	}

	ret = ivi_check_pending_desktop_surface_fullscreen(surface);
	if (ret) {
		ivi_set_desktop_surface_fullscreen(surface);
		ivi_layout_fullscreen_committed(surface);
		return;
	}

	ret = ivi_check_pending_desktop_surface_remote(surface);
	if (ret) {
		ivi_set_desktop_surface_remote(surface);
		ivi_layout_desktop_committed(surface);
		return;
	}

	/* if we end up here means we have a regular desktop app and
	 * try to activate it */
	ivi_set_desktop_surface(surface);
	ivi_layout_desktop_committed(surface);
}

void
ivi_shell_init_black_fs(struct ivi_compositor *ivi)
{
	struct ivi_output *out;

	wl_list_for_each(out, &ivi->outputs, link) {
		create_black_curtain_view(out);
		insert_black_curtain(out);
	}
}

int
ivi_shell_init(struct ivi_compositor *ivi)
{
	weston_layer_init(&ivi->hidden, ivi->compositor);
	weston_layer_init(&ivi->background, ivi->compositor);
	weston_layer_init(&ivi->normal, ivi->compositor);
	weston_layer_init(&ivi->panel, ivi->compositor);
	weston_layer_init(&ivi->popup, ivi->compositor);
	weston_layer_init(&ivi->fullscreen, ivi->compositor);

	weston_layer_set_position(&ivi->hidden,
				  WESTON_LAYER_POSITION_HIDDEN);
	weston_layer_set_position(&ivi->background,
				  WESTON_LAYER_POSITION_BACKGROUND);
	weston_layer_set_position(&ivi->normal,
				  WESTON_LAYER_POSITION_NORMAL);
	weston_layer_set_position(&ivi->panel,
				  WESTON_LAYER_POSITION_UI);
	weston_layer_set_position(&ivi->popup,
				  WESTON_LAYER_POSITION_TOP_UI);
	weston_layer_set_position(&ivi->fullscreen,
				  WESTON_LAYER_POSITION_FULLSCREEN);

	return 0;
}


static void
ivi_surf_destroy(struct ivi_surface *surf)
{
	struct weston_surface *wsurface = surf->view->surface;

	if (weston_surface_is_mapped(wsurface)) {
		weston_desktop_surface_unlink_view(surf->view);
		weston_view_destroy(surf->view);
	}

	wl_list_remove(&surf->link);
	free(surf);
}

static void
ivi_shell_destroy_views_on_layer(struct weston_layer *layer)
{
	struct weston_view *view, *view_next;

	wl_list_for_each_safe(view, view_next, &layer->view_list.link, layer_link.link) {
		struct ivi_surface *ivi_surf =
			get_ivi_shell_surface(view->surface);
		if (ivi_surf)
			ivi_surf_destroy(ivi_surf);
	}
}

void
ivi_shell_finalize(struct ivi_compositor *ivi)
{
	struct ivi_output *output;

	ivi_shell_destroy_views_on_layer(&ivi->hidden);
	weston_layer_fini(&ivi->hidden);

	ivi_shell_destroy_views_on_layer(&ivi->background);
	weston_layer_fini(&ivi->background);

	ivi_shell_destroy_views_on_layer(&ivi->normal);
	weston_layer_fini(&ivi->normal);

	ivi_shell_destroy_views_on_layer(&ivi->panel);
	weston_layer_fini(&ivi->panel);

	ivi_shell_destroy_views_on_layer(&ivi->popup);
	weston_layer_fini(&ivi->popup);

	wl_list_for_each(output, &ivi->outputs, link) {
		if (output->fullscreen_view.fs->view) {
			weston_surface_destroy(output->fullscreen_view.fs->view->surface);
			output->fullscreen_view.fs->view = NULL;
		}
	}
	weston_layer_fini(&ivi->fullscreen);
}

static void
ivi_shell_advertise_xdg_surfaces(struct ivi_compositor *ivi, struct wl_resource *resource)
{
	struct ivi_surface *surface;

	wl_list_for_each(surface, &ivi->surfaces, link) {
		const char *app_id =
			weston_desktop_surface_get_app_id(surface->dsurface);
		if (app_id == NULL) {
			weston_log("WARNING app_is is null, unable to advertise\n");
			return;
		}
		agl_shell_desktop_send_application(resource, app_id);
	}
}

static struct wl_client *
client_launch(struct weston_compositor *compositor,
		     struct weston_process *proc,
		     const char *path,
		     weston_process_cleanup_func_t cleanup)
{
	struct wl_client *client = NULL;
	struct custom_env child_env;
	struct fdstr wayland_socket;
	const char *fail_cloexec = "Couldn't unset CLOEXEC on client socket";
	const char *fail_seteuid = "Couldn't call seteuid";
	char *fail_exec;
	char * const *argp;
	char * const *envp;
	sigset_t allsigs;
	pid_t pid;
	bool ret;
	struct ivi_compositor *ivi;
	size_t written __attribute__((unused));

	weston_log("launching '%s'\n", path);
	str_printf(&fail_exec, "Error: Couldn't launch client '%s'\n", path);

	custom_env_init_from_environ(&child_env);
	custom_env_add_from_exec_string(&child_env, path);

	if (os_socketpair_cloexec(AF_UNIX, SOCK_STREAM, 0,
				  wayland_socket.fds) < 0) {
		weston_log("client_launch: "
			   "socketpair failed while launching '%s': %s\n",
			   path, strerror(errno));
		custom_env_fini(&child_env);
		return NULL;
	}
	fdstr_update_str1(&wayland_socket);
	custom_env_set_env_var(&child_env, "WAYLAND_SOCKET",
			       wayland_socket.str1);

	argp = custom_env_get_argp(&child_env);
	envp = custom_env_get_envp(&child_env);

	pid = fork();
	switch (pid) {
	case 0:
		/* Put the client in a new session so it won't catch signals
		 * intended for the parent. Sharing a session can be
		 * confusing when launching weston under gdb, as the ctrl-c
		 * intended for gdb will pass to the child, and weston
		 * will cleanly shut down when the child exits.
		 */
		setsid();

		/* do not give our signal mask to the new process */
		sigfillset(&allsigs);
		sigprocmask(SIG_UNBLOCK, &allsigs, NULL);

		/* Launch clients as the user. Do not launch clients with wrong euid. */
		if (seteuid(getuid()) == -1) {
			written = write(STDERR_FILENO, fail_seteuid, strlen(fail_seteuid));
			_exit(EXIT_FAILURE);
		}

		ret = fdstr_clear_cloexec_fd1(&wayland_socket);
		if (!ret) {
			written = write(STDERR_FILENO, fail_cloexec, strlen(fail_cloexec));
			_exit(EXIT_FAILURE);
		}

		execve(argp[0], argp, envp);

		if (fail_exec)
			written = write(STDERR_FILENO, fail_exec, strlen(fail_exec));
		_exit(EXIT_FAILURE);

	default:
		close(wayland_socket.fds[1]);
		ivi = weston_compositor_get_user_data(compositor);
		client = wl_client_create(compositor->wl_display,
					  wayland_socket.fds[0]);
		if (!client) {
			custom_env_fini(&child_env);
			close(wayland_socket.fds[0]);
			free(fail_exec);
			weston_log("client_launch: "
				"wl_client_create failed while launching '%s'.\n",
				path);
			return NULL;
		}

		proc->pid = pid;
		proc->cleanup = cleanup;
		wl_list_insert(&ivi->child_process_list, &proc->link);
		break;

	case -1:
		fdstr_close_all(&wayland_socket);
		weston_log("client_launch: "
			   "fork failed while launching '%s': %s\n", path,
			   strerror(errno));
		break;
	}

	custom_env_fini(&child_env);
	free(fail_exec);

	return client;
}

struct process_info {
	struct weston_process proc;
	char *path;
};

static void
process_handle_sigchld(struct weston_process *process, int status)
{
	struct process_info *pinfo =
		container_of(process, struct process_info, proc);

	/*
	 * There are no guarantees whether this runs before or after
	 * the wl_client destructor.
	 */

	if (WIFEXITED(status)) {
		weston_log("%s exited with status %d\n", pinfo->path,
			   WEXITSTATUS(status));
	} else if (WIFSIGNALED(status)) {
		weston_log("%s died on signal %d\n", pinfo->path,
			   WTERMSIG(status));
	} else {
		weston_log("%s disappeared\n", pinfo->path);
	}

	free(pinfo->path);
	free(pinfo);
}

int
ivi_launch_shell_client(struct ivi_compositor *ivi)
{
	struct process_info *pinfo;
	struct weston_config_section *section;
	char *command = NULL;

	section = weston_config_get_section(ivi->config, "shell-client",
					    NULL, NULL);
	if (section)
		weston_config_section_get_string(section, "command",
						 &command, NULL);

	if (!command)
		return -1;

	pinfo = zalloc(sizeof *pinfo);
	if (!pinfo)
		return -1;

	pinfo->path = strdup(command);
	if (!pinfo->path)
		goto out_free;

	ivi->shell_client.client = client_launch(ivi->compositor, &pinfo->proc,
						 command, process_handle_sigchld);
	if (!ivi->shell_client.client)
		goto out_str;

	return 0;

out_str:
	free(pinfo->path);

out_free:
	free(pinfo);

	return -1;
}

static void
destroy_black_curtain_view(struct wl_listener *listener, void *data)
{
	struct fullscreen_view *fs =
		wl_container_of(listener, fs, fs_destroy);


	if (fs && fs->fs) {
		wl_list_remove(&fs->fs_destroy.link);
		free(fs->fs);
	}
}


static void
create_black_curtain_view(struct ivi_output *output)
{
	struct weston_surface *surface = NULL;
	struct weston_view *view;
	struct ivi_compositor *ivi = output->ivi;
	struct weston_compositor *wc= ivi->compositor;
	struct weston_output *woutput = output->output;

	if (!woutput)
		return;

	surface = weston_surface_create(wc);
	if (!surface)
		return;
	view = weston_view_create(surface);
	if (!view) {
		weston_surface_destroy(surface);
		return;
	}

	weston_surface_set_color(surface, 0.0, 0.0, 0.0, 1);
	weston_surface_set_size(surface, woutput->width, woutput->height);
	weston_view_set_position(view, woutput->x, woutput->y);

	output->fullscreen_view.fs = zalloc(sizeof(struct ivi_surface));
	if (!output->fullscreen_view.fs) {
		weston_surface_destroy(surface);
		return;
	}
	output->fullscreen_view.fs->view = view;

	output->fullscreen_view.fs_destroy.notify = destroy_black_curtain_view;
	wl_signal_add(&woutput->destroy_signal,
		      &output->fullscreen_view.fs_destroy);
}

bool
output_has_black_curtain(struct ivi_output *output)
{
	return (output->fullscreen_view.fs->view &&
		output->fullscreen_view.fs->view->is_mapped &&
	        output->fullscreen_view.fs->view->surface->is_mapped);
}

void
remove_black_curtain(struct ivi_output *output)
{
	struct weston_view *view;

	if (!output &&
	    !output->fullscreen_view.fs &&
	    !output->fullscreen_view.fs->view) {
		weston_log("Output %s doesn't have a surface installed!\n", output->name);
		return;
	}

	view = output->fullscreen_view.fs->view;
	assert(view->is_mapped == true ||
	       view->surface->is_mapped == true);

	view->is_mapped = false;
	view->surface->is_mapped = false;

	weston_layer_entry_remove(&view->layer_link);
	weston_view_update_transform(view);

	weston_view_damage_below(view);

	weston_log("Removed black curtain from output %s\n", output->output->name);
}

void
insert_black_curtain(struct ivi_output *output)
{
	struct weston_view *view;

	if ((!output &&
	    !output->fullscreen_view.fs &&
	    !output->fullscreen_view.fs->view) || !output->output) {
		weston_log("Output %s doesn't have a surface installed!\n", output->name);
		return;
	}

	view = output->fullscreen_view.fs->view;
	if (view->is_mapped || view->surface->is_mapped)
		return;

	weston_layer_entry_remove(&view->layer_link);
	weston_layer_entry_insert(&output->ivi->fullscreen.view_list,
				  &view->layer_link);

	view->is_mapped = true;
	view->surface->is_mapped = true;

	weston_view_update_transform(view);
	weston_view_damage_below(view);

	weston_log("Added black curtain to output %s\n", output->output->name);
}

static void
shell_ready(struct wl_client *client, struct wl_resource *shell_res)
{
	struct ivi_compositor *ivi = wl_resource_get_user_data(shell_res);
	struct ivi_output *output;
	struct ivi_surface *surface, *tmp;

	if (ivi->shell_client.resource &&
	    ivi->shell_client.status == BOUND_FAILED) {
		wl_resource_post_error(shell_res,
				       WL_DISPLAY_ERROR_INVALID_OBJECT,
				       "agl_shell has already been bound. "
				       "Check out bound_fail event");
		return;
	}

	/* Init already finished. Do nothing */
	if (ivi->shell_client.ready)
		return;


	ivi->shell_client.ready = true;

	wl_list_for_each(output, &ivi->outputs, link) {
		if (output->background)
			remove_black_curtain(output);
		ivi_layout_init(ivi, output);
	}

	wl_list_for_each_safe(surface, tmp, &ivi->pending_surfaces, link) {
		const char *app_id;

		wl_list_remove(&surface->link);
		wl_list_init(&surface->link);
		ivi_check_pending_desktop_surface(surface);
		surface->checked_pending = true;
		app_id = weston_desktop_surface_get_app_id(surface->dsurface);

		if (app_id &&
		    wl_resource_get_version(ivi->shell_client.resource) >=
		    AGL_SHELL_APP_STATE_SINCE_VERSION)
			agl_shell_send_app_state(ivi->shell_client.resource,
						 app_id, AGL_SHELL_APP_STATE_STARTED);
	}
}

static void
shell_set_background(struct wl_client *client,
		     struct wl_resource *shell_res,
		     struct wl_resource *surface_res,
		     struct wl_resource *output_res)
{
	struct weston_head *head = weston_head_from_resource(output_res);
	struct weston_output *woutput = weston_head_get_output(head);
	struct ivi_output *output = to_ivi_output(woutput);
	struct weston_surface *wsurface = wl_resource_get_user_data(surface_res);
	struct ivi_compositor *ivi = wl_resource_get_user_data(shell_res);
	struct weston_desktop_surface *dsurface;
	struct ivi_surface *surface;

	if (ivi->shell_client.resource &&
	    ivi->shell_client.status == BOUND_FAILED) {
		wl_resource_post_error(shell_res,
				       WL_DISPLAY_ERROR_INVALID_OBJECT,
				       "agl_shell has already been bound. "
				       "Check out bound_fail event");
		return;
	}

	dsurface = weston_surface_get_desktop_surface(wsurface);
	if (!dsurface) {
		wl_resource_post_error(shell_res,
				       AGL_SHELL_ERROR_INVALID_ARGUMENT,
				       "surface must be a desktop surface");
		return;
	}

	surface = weston_desktop_surface_get_user_data(dsurface);
	if (surface->role != IVI_SURFACE_ROLE_NONE) {
		wl_resource_post_error(shell_res,
				       AGL_SHELL_ERROR_INVALID_ARGUMENT,
				       "surface already has another ivi role");
		return;
	}

	if (output->background) {
		wl_resource_post_error(shell_res,
				       AGL_SHELL_ERROR_BACKGROUND_EXISTS,
				       "output already has background");
		return;
	}

	surface->checked_pending = true;
	surface->role = IVI_SURFACE_ROLE_BACKGROUND;
	surface->bg.output = output;
	wl_list_remove(&surface->link);
	wl_list_init(&surface->link);

	output->background = surface;

	weston_desktop_surface_set_maximized(dsurface, true);
	weston_desktop_surface_set_size(dsurface,
					output->output->width,
					output->output->height);
}

static void
shell_set_panel(struct wl_client *client,
		struct wl_resource *shell_res,
		struct wl_resource *surface_res,
		struct wl_resource *output_res,
		uint32_t edge)
{
	struct weston_head *head = weston_head_from_resource(output_res);
	struct weston_output *woutput = weston_head_get_output(head);
	struct ivi_output *output = to_ivi_output(woutput);
	struct weston_surface *wsurface = wl_resource_get_user_data(surface_res);
	struct ivi_compositor *ivi = wl_resource_get_user_data(shell_res);
	struct weston_desktop_surface *dsurface;
	struct ivi_surface *surface;
	struct ivi_surface **member;
	int32_t width = 0, height = 0;

	if (ivi->shell_client.resource &&
	    ivi->shell_client.status == BOUND_FAILED) {
		wl_resource_post_error(shell_res,
				       WL_DISPLAY_ERROR_INVALID_OBJECT,
				       "agl_shell has already been bound. "
				       "Check out bound_fail event");
		return;
	}

	dsurface = weston_surface_get_desktop_surface(wsurface);
	if (!dsurface) {
		wl_resource_post_error(shell_res,
				       AGL_SHELL_ERROR_INVALID_ARGUMENT,
				       "surface must be a desktop surface");
		return;
	}

	surface = weston_desktop_surface_get_user_data(dsurface);
	if (surface->role != IVI_SURFACE_ROLE_NONE) {
		wl_resource_post_error(shell_res,
				       AGL_SHELL_ERROR_INVALID_ARGUMENT,
				       "surface already has another ivi role");
		return;
	}

	switch (edge) {
	case AGL_SHELL_EDGE_TOP:
		member = &output->top;
		break;
	case AGL_SHELL_EDGE_BOTTOM:
		member = &output->bottom;
		break;
	case AGL_SHELL_EDGE_LEFT:
		member = &output->left;
		break;
	case AGL_SHELL_EDGE_RIGHT:
		member = &output->right;
		break;
	default:
		wl_resource_post_error(shell_res,
				       AGL_SHELL_ERROR_INVALID_ARGUMENT,
				       "invalid edge for panel");
		return;
	}

	if (*member) {
		wl_resource_post_error(shell_res,
				       AGL_SHELL_ERROR_BACKGROUND_EXISTS,
				       "output already has panel on this edge");
		return;
	}

	surface->checked_pending = true;
	surface->role = IVI_SURFACE_ROLE_PANEL;
	surface->panel.output = output;
	surface->panel.edge = edge;
	wl_list_remove(&surface->link);
	wl_list_init(&surface->link);

	*member = surface;

	switch (surface->panel.edge) {
	case AGL_SHELL_EDGE_TOP:
	case AGL_SHELL_EDGE_BOTTOM:
		width = woutput->width;
		break;
	case AGL_SHELL_EDGE_LEFT:
	case AGL_SHELL_EDGE_RIGHT:
		height = woutput->height;
		break;
	}

	weston_desktop_surface_set_size(dsurface, width, height);
}

void
shell_advertise_app_state(struct ivi_compositor *ivi, const char *app_id,
			  const char *data, uint32_t app_state)
{
	struct desktop_client *dclient;
	uint32_t app_role;
	struct ivi_surface *surf = ivi_find_app(ivi, app_id);
	struct ivi_policy *policy = ivi->policy;

	/* FIXME: should queue it here and see when binding agl-shell-desktop
	 * if there are any to be sent */
	if (!surf)
		return;

	if (!app_id)
		return;

	if (policy && policy->api.surface_advertise_state_change &&
	    !policy->api.surface_advertise_state_change(surf, surf->ivi)) {
		return;
	}

	app_role = surf->role;
	if (app_role == IVI_SURFACE_ROLE_POPUP)
		app_role = AGL_SHELL_DESKTOP_APP_ROLE_POPUP;

	wl_list_for_each(dclient, &ivi->desktop_clients, link)
		agl_shell_desktop_send_state_app(dclient->resource, app_id,
						 data, app_state, app_role);
}

static void
shell_activate_app(struct wl_client *client,
		   struct wl_resource *shell_res,
		   const char *app_id,
		   struct wl_resource *output_res)
{
	struct weston_head *head = weston_head_from_resource(output_res);
	struct weston_output *woutput = weston_head_get_output(head);
	struct ivi_compositor *ivi = wl_resource_get_user_data(shell_res);
	struct ivi_output *output = to_ivi_output(woutput);

	if (ivi->shell_client.resource &&
	    ivi->shell_client.status == BOUND_FAILED) {
		wl_resource_post_error(shell_res,
				       WL_DISPLAY_ERROR_INVALID_OBJECT,
				       "agl_shell has already been bound. "
				       "Check out bound_fail event");
		return;
	}

	ivi_layout_activate(output, app_id);
}

static void
shell_desktop_activate_app(struct wl_client *client,
			   struct wl_resource *shell_res,
			   const char *app_id, const char *data,
			   struct wl_resource *output_res)
{
	struct weston_head *head = weston_head_from_resource(output_res);
	struct weston_output *woutput = weston_head_get_output(head);
	struct ivi_output *output = to_ivi_output(woutput);

	ivi_layout_activate(output, app_id);
	shell_advertise_app_state(output->ivi, app_id,
				  data, AGL_SHELL_DESKTOP_APP_STATE_ACTIVATED);
}

static void
shell_deactivate_app(struct wl_client *client,
		   struct wl_resource *shell_res,
		   const char *app_id)
{
	struct desktop_client *dclient = wl_resource_get_user_data(shell_res);
	struct ivi_compositor *ivi = dclient->ivi;

	ivi_layout_deactivate(ivi, app_id);
	shell_advertise_app_state(ivi, app_id,
				  NULL, AGL_SHELL_DESKTOP_APP_STATE_DEACTIVATED);
}

/* stub, no usage for the time being */
static void
shell_destroy(struct wl_client *client, struct wl_resource *res)
{
}

static const struct agl_shell_interface agl_shell_implementation = {
	.destroy = shell_destroy,
	.ready = shell_ready,
	.set_background = shell_set_background,
	.set_panel = shell_set_panel,
	.activate_app = shell_activate_app,
};

static void
shell_desktop_set_app_property(struct wl_client *client,
			       struct wl_resource *shell_res,
			       const char *app_id, uint32_t role,
			       int x, int y, int bx, int by,
			       int width, int height,
			       struct wl_resource *output_res)
{
	struct weston_head *head = weston_head_from_resource(output_res);
	struct weston_output *woutput = weston_head_get_output(head);
	struct ivi_output *output = to_ivi_output(woutput);

	switch (role) {
	case AGL_SHELL_DESKTOP_APP_ROLE_POPUP:
		ivi_set_pending_desktop_surface_popup(output, x, y, bx, by,
						      width, height, app_id);
		break;
	case AGL_SHELL_DESKTOP_APP_ROLE_FULLSCREEN:
		ivi_set_pending_desktop_surface_fullscreen(output, app_id);
		break;
	case AGL_SHELL_DESKTOP_APP_ROLE_SPLIT_VERTICAL:
	case AGL_SHELL_DESKTOP_APP_ROLE_SPLIT_HORIZONTAL:
		ivi_set_pending_desktop_surface_split(output, app_id, role);
		break;
	case AGL_SHELL_DESKTOP_APP_ROLE_REMOTE:
		ivi_set_pending_desktop_surface_remote(output, app_id);
		break;
	default:
		break;
	}
}

void
ivi_compositor_destroy_pending_surfaces(struct ivi_compositor *ivi)
{
	struct pending_popup *p_popup, *next_p_popup;
	struct pending_split *split_surf, *next_split_surf;
	struct pending_fullscreen *fs_surf, *next_fs_surf;
	struct pending_remote *remote_surf, *next_remote_surf;

	wl_list_for_each_safe(p_popup, next_p_popup,
			      &ivi->popup_pending_apps, link)
		ivi_remove_pending_desktop_surface_popup(p_popup);

	wl_list_for_each_safe(split_surf, next_split_surf,
			      &ivi->split_pending_apps, link)
		ivi_remove_pending_desktop_surface_split(split_surf);

	wl_list_for_each_safe(fs_surf, next_fs_surf,
			      &ivi->fullscreen_pending_apps, link)
		ivi_remove_pending_desktop_surface_fullscreen(fs_surf);

	wl_list_for_each_safe(remote_surf, next_remote_surf,
			      &ivi->remote_pending_apps, link)
		ivi_remove_pending_desktop_surface_remote(remote_surf);
}

static void
shell_desktop_set_app_property_mode(struct wl_client *client,
				    struct wl_resource *shell_res, uint32_t perm)
{
	struct desktop_client *dclient = wl_resource_get_user_data(shell_res);
	if (perm) {
		dclient->ivi->keep_pending_surfaces = true;
	} else {
		dclient->ivi->keep_pending_surfaces = false;
		/* remove any previous pending surfaces */
		ivi_compositor_destroy_pending_surfaces(dclient->ivi);
	}
}

static const struct agl_shell_desktop_interface agl_shell_desktop_implementation = {
	.activate_app = shell_desktop_activate_app,
	.set_app_property = shell_desktop_set_app_property,
	.deactivate_app = shell_deactivate_app,
	.set_app_property_mode = shell_desktop_set_app_property_mode,
};

static void
unbind_agl_shell(struct wl_resource *resource)
{
	struct ivi_compositor *ivi;
	struct ivi_output *output;
	struct ivi_surface *surf, *surf_tmp;

	ivi = wl_resource_get_user_data(resource);

	/* reset status to allow other clients issue legit requests */
	if (ivi->shell_client.resource &&
	    ivi->shell_client.status == BOUND_FAILED) {
		ivi->shell_client.status = BOUND_OK;
		return;
	}

	wl_list_for_each(output, &ivi->outputs, link) {
		/* reset the active surf if there's one present */
		if (output->active) {
			output->active->view->is_mapped = false;
			output->active->view->surface->is_mapped = false;

			weston_layer_entry_remove(&output->active->view->layer_link);
			output->active = NULL;
		}

		insert_black_curtain(output);
	}

	wl_list_for_each_safe(surf, surf_tmp, &ivi->surfaces, link) {
		wl_list_remove(&surf->link);
		wl_list_init(&surf->link);
	}

	wl_list_for_each_safe(surf, surf_tmp, &ivi->pending_surfaces, link) {
		wl_list_remove(&surf->link);
		wl_list_init(&surf->link);
	}

	wl_list_init(&ivi->surfaces);
	wl_list_init(&ivi->pending_surfaces);

	ivi->shell_client.ready = false;
	ivi->shell_client.resource = NULL;
	ivi->shell_client.client = NULL;
}

static void
bind_agl_shell(struct wl_client *client,
	       void *data, uint32_t version, uint32_t id)
{
	struct ivi_compositor *ivi = data;
	struct wl_resource *resource;
	struct ivi_policy *policy;
	void *interface;

	policy = ivi->policy;
	interface = (void *) &agl_shell_interface;
	if (policy && policy->api.shell_bind_interface &&
	    !policy->api.shell_bind_interface(client, interface)) {
		wl_client_post_implementation_error(client,
				       "client not authorized to use agl_shell");
		return;
	}

	resource = wl_resource_create(client, &agl_shell_interface, version, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}

	if (ivi->shell_client.resource) {
		if (wl_resource_get_version(resource) == 1) {
			wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
					       "agl_shell has already been bound");
			return;
		}

		agl_shell_send_bound_fail(resource);
		ivi->shell_client.status = BOUND_FAILED;
	}

	wl_resource_set_implementation(resource, &agl_shell_implementation,
				       ivi, unbind_agl_shell);
	ivi->shell_client.resource = resource;

	if (ivi->shell_client.status == BOUND_OK &&
	    wl_resource_get_version(resource) >= AGL_SHELL_BOUND_OK_SINCE_VERSION)
		agl_shell_send_bound_ok(ivi->shell_client.resource);
}

static void
unbind_agl_shell_desktop(struct wl_resource *resource)
{
	struct desktop_client *dclient = wl_resource_get_user_data(resource);

	wl_list_remove(&dclient->link);
	free(dclient);
}

static void
bind_agl_shell_desktop(struct wl_client *client,
		       void *data, uint32_t version, uint32_t id)
{
	struct ivi_compositor *ivi = data;
	struct wl_resource *resource;
	struct ivi_policy *policy;
	struct desktop_client *dclient;
	void *interface;

	policy = ivi->policy;
	interface  = (void *) &agl_shell_desktop_interface;
	if (policy && policy->api.shell_bind_interface &&
	    !policy->api.shell_bind_interface(client, interface)) {
		wl_client_post_implementation_error(client,
				"client not authorized to use agl_shell_desktop");
		return;
	}

	dclient = zalloc(sizeof(*dclient));
	if (!dclient) {
		wl_client_post_no_memory(client);
		return;
	}

	resource = wl_resource_create(client, &agl_shell_desktop_interface,
				      version, id);
	dclient->ivi = ivi;
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(resource, &agl_shell_desktop_implementation,
				       dclient, unbind_agl_shell_desktop);

	dclient->resource = resource;
	wl_list_insert(&ivi->desktop_clients, &dclient->link);

	/* advertise xdg surfaces */
	ivi_shell_advertise_xdg_surfaces(ivi, resource);
}

int
ivi_shell_create_global(struct ivi_compositor *ivi)
{
	ivi->agl_shell = wl_global_create(ivi->compositor->wl_display,
					  &agl_shell_interface, 3,
					  ivi, bind_agl_shell);
	if (!ivi->agl_shell) {
		weston_log("Failed to create wayland global.\n");
		return -1;
	}

	ivi->agl_shell_desktop = wl_global_create(ivi->compositor->wl_display,
						  &agl_shell_desktop_interface, 2,
						  ivi, bind_agl_shell_desktop);
	if (!ivi->agl_shell_desktop) {
		weston_log("Failed to create wayland global (agl_shell_desktop).\n");
		return -1;
	}

	return 0;
}
