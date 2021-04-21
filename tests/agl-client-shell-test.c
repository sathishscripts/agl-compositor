#include "config.h"

#include <stdint.h>
#include <stdio.h>

#include "weston-test-client-helper.h"
#include "weston-test-fixture-compositor.h"

#include "agl-shell-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#include "test-config.h"

#define WINDOW_WIDTH_SIZE	200
#define WINDOW_HEIGHT_SIZE	200

enum window_type {
	BACKGROUND 	= -1,
	PANEL_TOP	= 0,
	PANEL_BOTTOM	= 1,
	PANEL_LEFT	= 2,
	PANEL_RIGHT	= 3
};

pixman_color_t bg_color = {
	.red   = 0x0000,
	.green = 0x0000,
	.blue  = 0xffff,
	.alpha = 0xffff
};

pixman_color_t panel_top_color = {
	.red   = 0xffff,
	.green = 0x0000,
	.blue  = 0x0000,
	.alpha = 0xffff
};

pixman_color_t panel_bottom_color = {
	.red   = 0x0000,
	.green = 0xffff,
	.blue  = 0x0000,
	.alpha = 0xffff
};

struct display {
	struct agl_shell *agl_shell;
	struct xdg_wm_base *wm_base;
	struct client *client;
	struct wl_list win_list;
};

struct window {
	struct display *display;
	struct xdg_toplevel *xdg_toplevel;
	struct xdg_surface *xdg_surface;
	struct wl_surface *surface;
	struct buffer *buffer;

	bool wait_for_configure;

	int width;
	int height;
	bool maximized;
	bool fullscreen;
	enum window_type w_type;

	struct wl_list link;
};

static enum test_result_code
fixture_setup(struct weston_test_harness *harness)
{
	struct compositor_setup setup;

	compositor_setup_defaults(&setup);
	setup.renderer = RENDERER_PIXMAN;
	setup.width = 1920;
	setup.height = 1080;

	return weston_test_harness_execute_as_client(harness, &setup);
}

DECLARE_FIXTURE_SETUP(fixture_setup);

static struct window *
create_window(int width, int height)
{
	struct window *window = calloc(1, sizeof(*window));

	window->width = width;
	window->height = height;

	return window;
}

static struct display *
create_display(struct client *client, struct xdg_wm_base *wm_base, struct agl_shell *agl_shell)
{
	struct display *display = calloc(1, sizeof(*display));

	display->client = client;
	display->wm_base = wm_base;
	display->agl_shell = agl_shell;

	return display;
}

static void
xdg_wm_base_ping(void *data, struct xdg_wm_base *shell, uint32_t serial)
{
	xdg_wm_base_pong(shell, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
	xdg_wm_base_ping,
};

static void
draw(struct window *window, pixman_color_t color)
{
	struct client *client = window->display->client;

	testlog("Creating a buffer with %dx%d\n", window->width, window->height);
	window->buffer =
		create_shm_buffer_a8r8g8b8(client, window->width, window->height);
	fill_image_with_color(window->buffer->image, &color);

	wl_surface_attach(window->surface, window->buffer->proxy, 0, 0);
	wl_surface_damage(window->surface, 0, 0, window->width, window->height);

	wl_surface_commit(window->surface);
}

static void
handle_xdg_surface_configure(void *data, struct xdg_surface *surface, uint32_t serial)
{
        struct window *window = data;
        xdg_surface_ack_configure(surface, serial);

	if (window->wait_for_configure) {
		switch (window->w_type) {
		case BACKGROUND:
			draw(window, bg_color);
			break;
		case PANEL_TOP:
			draw(window, panel_top_color);
			break;
		case PANEL_BOTTOM:
			draw(window, panel_bottom_color);
			break;
		case PANEL_LEFT:
		case PANEL_RIGHT:
			break;
		}
		window->wait_for_configure = false;
	}
}

static const struct xdg_surface_listener xdg_surface_listener = {
	handle_xdg_surface_configure,
};

static void
handle_xdg_toplevel_configure(void *data, struct xdg_toplevel *xdg_toplevel,
                              int32_t width, int32_t height, struct wl_array *states)
{
	struct window *window = data;
	uint32_t *p;

	window->fullscreen = 0;
	window->maximized = 0;

	wl_array_for_each(p, states) {
		uint32_t state = *p;
		switch (state) {
		case XDG_TOPLEVEL_STATE_FULLSCREEN:
			window->fullscreen = 1;
			break;
		case XDG_TOPLEVEL_STATE_MAXIMIZED:
			window->maximized = 1;
			break;
		}
	}

	if (width > 0 && height > 0) {
		if (!window->fullscreen && !window->maximized) {
			window->width = width;
			window->height = height;
		}
		window->width = width;
		window->height = height;
	} else if (!window->fullscreen && !window->maximized) {
		if (width == 0)
			window->width = WINDOW_WIDTH_SIZE;
		else
			window->width = width;

		if (height == 0)
			window->height = WINDOW_HEIGHT_SIZE;
		else
			window->height = height;
	}

	/* if we've been resized set wait_for_configure to adjust the fb size
	 * in the frame callback handler, which will also clear this up */
	if ((window->width > 0 && window->width != WINDOW_WIDTH_SIZE) &&
	    (window->height > 0 && window->height != WINDOW_HEIGHT_SIZE)) {
		window->wait_for_configure = true;
	}
}

static void
handle_xdg_toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel)
{
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
        handle_xdg_toplevel_configure,
        handle_xdg_toplevel_close,
};


static struct window *
setup_agl_shell_client_bg(struct display *display)
{
	struct window *window;
	window = create_window(200, 200);

	window->display = display;

	xdg_wm_base_add_listener(display->wm_base, &xdg_wm_base_listener, display);

	window->surface =
		wl_compositor_create_surface(display->client->wl_compositor);
	window->xdg_surface =
		xdg_wm_base_get_xdg_surface(display->wm_base, window->surface);
	assert(window->xdg_surface);

	xdg_surface_add_listener(window->xdg_surface, &xdg_surface_listener, window);
	window->xdg_toplevel = xdg_surface_get_toplevel(window->xdg_surface);
	assert(window->xdg_toplevel);

	xdg_toplevel_add_listener(window->xdg_toplevel, &xdg_toplevel_listener, window);

	xdg_toplevel_set_title(window->xdg_toplevel, "bg");
	xdg_toplevel_set_app_id(window->xdg_toplevel, "bg");

	wl_surface_commit(window->surface);

	window->wait_for_configure = true;

	agl_shell_set_background(display->agl_shell, window->surface,
				 display->client->output->wl_output);

	window->w_type = BACKGROUND;
	return window;
}

static struct window *
setup_agl_shell_client_panel(struct display *display, enum agl_shell_edge edge)
{
	struct window *window;
	window = create_window(200, 200);

	window->display = display;

	xdg_wm_base_add_listener(display->wm_base,
				 &xdg_wm_base_listener, display);

	window->surface =
		wl_compositor_create_surface(display->client->wl_compositor);
	window->xdg_surface =
		xdg_wm_base_get_xdg_surface(display->wm_base, window->surface);
	assert(window->xdg_surface);

	xdg_surface_add_listener(window->xdg_surface,
				 &xdg_surface_listener, window);
	window->xdg_toplevel = xdg_surface_get_toplevel(window->xdg_surface);
	assert(window->xdg_toplevel);

	xdg_toplevel_add_listener(window->xdg_toplevel,
				  &xdg_toplevel_listener, window);

	switch (edge) {
	case AGL_SHELL_EDGE_TOP:
		xdg_toplevel_set_title(window->xdg_toplevel, "panel top");
		xdg_toplevel_set_app_id(window->xdg_toplevel, "panel top");
		break;
	case AGL_SHELL_EDGE_BOTTOM:
		xdg_toplevel_set_title(window->xdg_toplevel, "panel bottom");
		xdg_toplevel_set_app_id(window->xdg_toplevel, "panel bottom");
		break;
	case AGL_SHELL_EDGE_LEFT:
	case AGL_SHELL_EDGE_RIGHT:
		break;
	}

	wl_surface_commit(window->surface);

	window->wait_for_configure = true;

	agl_shell_set_panel(display->agl_shell, window->surface,
			    display->client->output->wl_output, edge);

	window->w_type = (enum window_type) edge;
	return window;
}

static struct display *
setup_agl_shell_client(struct client *client)
{
	struct display *display;
	struct agl_shell *agl_shell;
	struct xdg_wm_base *wm_base;

	wm_base = bind_to_singleton_global(client, &xdg_wm_base_interface, 1);
	assert(wm_base);

	agl_shell = bind_to_singleton_global(client, &agl_shell_interface, 1);
	assert(agl_shell);

	display = create_display(client, wm_base, agl_shell);
	wl_list_init(&display->win_list);

	struct window *win_bg = setup_agl_shell_client_bg(display);
	wl_list_insert(&display->win_list, &win_bg->link);

	struct window *win_panel_top =
		setup_agl_shell_client_panel(display, AGL_SHELL_EDGE_TOP);
	wl_list_insert(&display->win_list, &win_panel_top->link);

	struct window *win_panel_bottom =
		setup_agl_shell_client_panel(display, AGL_SHELL_EDGE_BOTTOM);
	wl_list_insert(&display->win_list, &win_panel_bottom->link);

	client_roundtrip(client);

	/* send ready() */
	agl_shell_ready(agl_shell);
	return display;
}

static void
display_destroy(struct display *display)
{
	struct window *win, *win_next;

	wl_list_for_each_safe(win, win_next, &display->win_list, link) {
		wl_list_remove(&win->link);
		free(win);
	}
}

TEST(agl_client_shell)
{
	struct display *display;
	struct client *client = create_client();
	bool match;

	assert(client);

	/* Create the client */
	testlog("Creating client shell for agl-shell\n");
	display = setup_agl_shell_client(client);

	client_roundtrip(client);

	/* take a screenshot and compare it with reference -> 
	 * agl-shell client shell works */
	match = verify_screen_content(client, "agl_client_shell", 0, NULL, 0);
	assert(match);

	client_destroy(client);
	display_destroy(display);
}
