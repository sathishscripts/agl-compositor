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

#include <assert.h>
#include <string.h>

#include <libweston/libweston.h>
#include <libweston-desktop/libweston-desktop.h>

#define AGL_COMP_DEBUG

static void
ivi_background_init(struct ivi_compositor *ivi, struct ivi_output *output)
{
	struct weston_output *woutput = output->output;
	struct ivi_surface *bg = output->background;
	struct weston_view *view;

	if (!bg) {
		weston_log("WARNING: Output does not have a background\n");
		return;
	}

	assert(bg->role == IVI_SURFACE_ROLE_BACKGROUND);

	view = bg->view;

	weston_view_set_output(view, woutput);
	weston_view_set_position(view, woutput->x, woutput->y);

#ifdef AGL_COMP_DEBUG
	weston_log("(background) position view %p, x %d, y %d\n", view,
			woutput->x, woutput->y);
#endif

	view->is_mapped = true;
	view->surface->is_mapped = true;

	weston_layer_entry_insert(&ivi->background.view_list, &view->layer_link);
}

static void
ivi_panel_init(struct ivi_compositor *ivi, struct ivi_output *output,
	       struct ivi_surface *panel)
{
	struct weston_output *woutput = output->output;
	struct weston_desktop_surface *dsurface;
	struct weston_view *view;
	struct weston_geometry geom;
	int x = woutput->x;
	int y = woutput->y;

	if (!panel)
		return;

	assert(panel->role == IVI_SURFACE_ROLE_PANEL);
	dsurface = panel->dsurface;
	view = panel->view;
	geom = weston_desktop_surface_get_geometry(dsurface);
#ifdef AGL_COMP_DEBUG
	weston_log("geom.width %d, geom.height %d, geom.x %d, geom.y %d\n",
			geom.width, geom.height, geom.x, geom.y);
#endif
	switch (panel->panel.edge) {
	case AGL_SHELL_EDGE_TOP:
		output->area.y += geom.height;
		output->area.height -= geom.height;
		break;
	case AGL_SHELL_EDGE_BOTTOM:
		y += woutput->height - geom.height;
		output->area.height -= geom.height;
		break;
	case AGL_SHELL_EDGE_LEFT:
		output->area.x += geom.width;
		output->area.width -= geom.width;
		break;
	case AGL_SHELL_EDGE_RIGHT:
		x += woutput->width - geom.width;
		output->area.width -= geom.width;
		break;
	}

	x -= geom.x;
	y -= geom.y;

	weston_view_set_output(view, woutput);
	weston_view_set_position(view, x, y);
#ifdef AGL_COMP_DEBUG
	weston_log("(panel) edge %d position view %p, x %d, y %d\n",
			panel->panel.edge, view, x, y);
#endif

	/* this is necessary for cases we already mapped it desktop_committed()
	 * but we not running the older qtwayland, so we still have a chance
	 * for this to run at the next test */
	if (view->surface->is_mapped) {
		weston_layer_entry_remove(&view->layer_link);

		view->is_mapped = false;
		view->surface->is_mapped = false;
	}

	/* give ivi_layout_panel_committed() a chance to map the view/surface
	 * instead */
	if ((geom.width == geom.height && geom.width == 0) &&
	    (geom.x == geom.y && geom.x == 0) &&
	    panel->panel.edge != AGL_SHELL_EDGE_TOP)
		return;

	view->is_mapped = true;
	view->surface->is_mapped = true;
#ifdef AGL_COMP_DEBUG
	weston_log("panel type %d inited\n", panel->panel.edge);
#endif
	weston_layer_entry_insert(&ivi->panel.view_list, &view->layer_link);
}

/*
 * Initializes all static parts of the layout, i.e. the background and panels.
 */
void
ivi_layout_init(struct ivi_compositor *ivi, struct ivi_output *output)
{
	ivi_background_init(ivi, output);

	output->area.x = 0;
	output->area.y = 0;
	output->area.width = output->output->width;
	output->area.height = output->output->height;

	ivi_panel_init(ivi, output, output->top);
	ivi_panel_init(ivi, output, output->bottom);
	ivi_panel_init(ivi, output, output->left);
	ivi_panel_init(ivi, output, output->right);

	weston_compositor_schedule_repaint(ivi->compositor);

	weston_log("Usable area: %dx%d+%d,%d\n",
		   output->area.width, output->area.height,
		   output->area.x, output->area.y);
}

static struct ivi_surface *
ivi_find_app(struct ivi_compositor *ivi, const char *app_id)
{
	struct ivi_surface *surf;
	const char *id;

	wl_list_for_each(surf, &ivi->surfaces, link) {
		id = weston_desktop_surface_get_app_id(surf->dsurface);
		if (id && strcmp(app_id, id) == 0)
			return surf;
	}

	return NULL;
}

static void
ivi_layout_activate_complete(struct ivi_output *output,
			     struct ivi_surface *surf)
{
	struct ivi_compositor *ivi = output->ivi;
	struct weston_output *woutput = output->output;
	struct weston_view *view = surf->view;

	if (weston_view_is_mapped(view)) {
		weston_layer_entry_remove(&view->layer_link);
	}

	weston_view_set_output(view, woutput);
	weston_view_set_position(view,
				 woutput->x + output->area.x,
				 woutput->y + output->area.y);

	view->is_mapped = true;
	view->surface->is_mapped = true;

	if (output->active) {
		output->active->view->is_mapped = false;
		output->active->view->surface->is_mapped = false;

		weston_layer_entry_remove(&output->active->view->layer_link);
	}
	output->active = surf;

	weston_layer_entry_insert(&ivi->normal.view_list, &view->layer_link);
	weston_view_update_transform(view);

	/* force repaint of the entire output */
	weston_output_damage(output->output);
	surf->desktop.last_output = surf->desktop.pending_output;
	surf->desktop.pending_output = NULL;
}

static struct ivi_output *
ivi_layout_find_bg_output(struct ivi_compositor *ivi)
{
	struct ivi_output *out;

	wl_list_for_each(out, &ivi->outputs, link) {
		if (out->background &&
		    out->background->role == IVI_SURFACE_ROLE_BACKGROUND)
			return out;
	}

	return NULL;
}

void
ivi_layout_desktop_committed(struct ivi_surface *surf)
{
	struct weston_desktop_surface *dsurf = surf->dsurface;
	struct weston_geometry geom = weston_desktop_surface_get_geometry(dsurf);
	struct ivi_output *output;

	assert(surf->role == IVI_SURFACE_ROLE_DESKTOP);

	output = surf->desktop.pending_output;
	if (!output) {
		struct ivi_output *ivi_bg_output;

		/* FIXME: This should be changed to determine if the policy
		 * database allows that to happen */
		if (!surf->ivi->quirks.activate_apps_by_default)
			return;

		ivi_bg_output = ivi_layout_find_bg_output(surf->ivi);

		/* use the output of the bg to activate the app on start-up by
		 * default */
		if (surf->view && ivi_bg_output) {
			const char *app_id =
				weston_desktop_surface_get_app_id(dsurf);
			if (app_id && ivi_bg_output)
				ivi_layout_activate(ivi_bg_output, app_id);
		}

		return;
	}

	if (!weston_desktop_surface_get_maximized(dsurf) ||
	    geom.width != output->area.width ||
	    geom.height != output->area.height)
		return;

	ivi_layout_activate_complete(output, surf);
}

void
ivi_layout_panel_committed(struct ivi_surface *surface)
{
	struct ivi_compositor *ivi = surface->ivi;
	struct ivi_output *output = surface->bg.output;
	struct weston_output *woutput = output->output;
	struct weston_desktop_surface *dsurface = surface->dsurface;
	struct weston_surface *wsurface =
		weston_desktop_surface_get_surface(dsurface);
	struct weston_geometry geom;
	int x = woutput->x;
	int y = woutput->y;

	assert(surface->role == IVI_SURFACE_ROLE_PANEL);

	/*
	 * If the desktop_surface geometry is not set and the panel is not a
	 * top one, we'll give this a chance to run, as some qtwayland version
	 * seem to have a 'problem', where the panel initilization part will
	 * have a desktop surface with 0 as geometry for *all* its members
	 * (width/height). Doing that will result in the panel not being
	 * displayed at all.
	 *
	 * Later versions of qtwayland do have the correct window geometry for
	 * the desktop surface so the weston_surface is already mapped in
	 * ivi_panel_init().
	 */
	if (wsurface->is_mapped)
		return;

	geom = weston_desktop_surface_get_geometry(dsurface);

#ifdef AGL_COMP_DEBUG
	weston_log("geom.width %d, geom.height %d, geom.x %d, geom.y %d\n",
			geom.width, geom.height, geom.x, geom.y);
#endif

	switch (surface->panel.edge) {
	case AGL_SHELL_EDGE_TOP:
		/* Do nothing */
		break;
	case AGL_SHELL_EDGE_BOTTOM:
		y += woutput->height - geom.height;
		break;
	case AGL_SHELL_EDGE_LEFT:
		/* Do nothing */
		break;
	case AGL_SHELL_EDGE_RIGHT:
		x += woutput->width - geom.width;
		break;
	}
#ifndef AGL_COMP_DEBUG
	weston_log("panel type %d commited\n", surface->panel.edge);
#endif

	weston_view_set_output(surface->view, woutput);
	weston_view_set_position(surface->view, x, y);
	weston_layer_entry_insert(&ivi->panel.view_list,
				  &surface->view->layer_link);

	weston_view_update_transform(surface->view);
	weston_view_schedule_repaint(surface->view);

	wsurface->is_mapped = true;
	surface->view->is_mapped = true;
}

void
ivi_layout_activate(struct ivi_output *output, const char *app_id)
{
	struct ivi_compositor *ivi = output->ivi;
	struct ivi_surface *surf;
	struct weston_desktop_surface *dsurf;
	struct weston_view *view;
	struct weston_geometry geom;

	surf = ivi_find_app(ivi, app_id);
	if (!surf)
		return;
#ifdef AGL_COMP_DEBUG
	weston_log("Found app_id %s\n", app_id);
#endif
	if (surf == output->active)
		return;

	dsurf = surf->dsurface;
	view = surf->view;
	geom = weston_desktop_surface_get_geometry(dsurf);

	if (weston_desktop_surface_get_maximized(dsurf) &&
	    geom.width == output->area.width &&
	    geom.height == output->area.height) {
		ivi_layout_activate_complete(output, surf);
		return;
	}

	weston_desktop_surface_set_maximized(dsurf, true);
	weston_desktop_surface_set_size(dsurf,
					output->area.width,
					output->area.height);

	/*
	 * If the view isn't mapped, we put it onto the hidden layer so it will
	 * start receiving frame events, and will be able to act on our
	 * configure event.
	 */
	if (!weston_view_is_mapped(view)) {
		view->is_mapped = true;
		view->surface->is_mapped = true;

		weston_view_set_output(view, output->output);
		weston_layer_entry_insert(&ivi->hidden.view_list, &view->layer_link);
		/* force repaint of the entire output */
		weston_output_damage(output->output);
	}

	surf->desktop.pending_output = output;
}
