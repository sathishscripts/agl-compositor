#include "config.h"

#include <stdint.h>
#include <stdio.h>

#include "weston-test-client-helper.h"
#include "weston-test-fixture-compositor.h"
#include "test-config.h"

static enum test_result_code
fixture_setup(struct weston_test_harness *harness)
{
	struct compositor_setup setup;

	compositor_setup_defaults(&setup);
	setup.renderer = RENDERER_PIXMAN;
	setup.width = 320;
	setup.height = 240;
	setup.shell = SHELL_DESKTOP;

	return weston_test_harness_execute_as_client(harness, &setup);
}

DECLARE_FIXTURE_SETUP(fixture_setup);


TEST(agl_shell)
{
	struct client *client;
	struct wl_surface *surface;

	/* Create the client */
	testlog("Creating client for test\n");

	client = create_client_and_test_surface(100, 100, 100, 100);
	assert(client);

	surface = client->surface->wl_surface;
	(void) surface;

	testlog("Test complete\n");

	client_destroy(client);
}
