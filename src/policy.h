#ifndef POLICY_H
#define POLICY_H

#include "ivi-compositor.h"
#include "agl-shell-policy-server-protocol.h"

struct ivi_policy;

struct state_event {
	uint32_t value;
	char *name;
	struct wl_list link;	/* ivi_policy::states or ivi_policy::events */
};

struct ivi_a_policy {
	struct ivi_policy *policy;

	char *app_id;
	uint32_t state;
	uint32_t event;
	uint32_t timeout;
	struct ivi_output *output;
	struct wl_event_source *timer;

	struct wl_list link;	/* ivi_policy::ivi_policies */
};

struct ivi_policy_api {
	size_t struct_size;

	bool (*surface_create)(struct ivi_surface *surf, void *user_data);
	bool (*surface_commited)(struct ivi_surface *surf, void *user_data);
	bool (*surface_activate)(struct ivi_surface *surf, void *user_data);

	bool (*policy_rule_allow_to_add)(void *user_data);
	void (*policy_rule_try_event)(struct ivi_a_policy *a_policy);
};

struct ivi_policy {
	struct ivi_compositor *ivi;
	struct ivi_policy_api api;
	void *user_data;

	/* used to inject policies back to the compositor */
	struct wl_global *policy_shell;
	struct wl_resource *resource;
	struct wl_list policies;	/* ivi_policy_inject::link */

	uint32_t current_state;
	uint32_t previous_state;
	bool state_change_in_progress;

	struct wl_list states;	/* state_event::link */
	struct wl_list events;	/* state_event::link */

	struct wl_listener listener_check_policies;
	struct wl_signal signal_state_change;
};


struct ivi_policy *
ivi_policy_create(struct ivi_compositor *compositor,
                  const struct ivi_policy_api *api, void *user_data);
void
ivi_policy_destroy(struct ivi_policy *ivi_policy);

int
ivi_policy_init(struct ivi_compositor *ivi);

#endif
