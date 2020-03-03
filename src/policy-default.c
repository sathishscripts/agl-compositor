#include "ivi-compositor.h"
#include "policy.h"

/*
 * default policy implementation allows every action to be possible
 *
 * This is an example, that implements the API
 *
 * - policy_rule_allow_to_add is required in order to add further policy rules
 * - policy_rule_try_event is a hook that should be implemented in order
 *   for agl-shell-policy to work correctly.
 */
static bool
ivi_policy_default_surface_create(struct ivi_surface *surf, void *user_data)
{
	/* verify that the surface should be created */
	return true;
}

static bool
ivi_policy_default_surface_commmited(struct ivi_surface *surf, void *user_data)
{
	/* verify that the surface should be commited */
	return true;
}

static bool
ivi_policy_default_surface_activate(struct ivi_surface *surf, void *user_data)
{
	/* verify that the surface shuld be switched to */
	return true;
}

static bool
ivi_policy_default_allow_to_add(void *user_data)
{
	/* verify that it can inject events with the protocol */
	return true;
}

static void
ivi_policy_default_try_event(struct ivi_a_policy *a_policy)
{
	uint32_t event = a_policy->event;

	switch (event) {
	case AGL_SHELL_POLICY_EVENT_SHOW:
		ivi_layout_activate(a_policy->output, a_policy->app_id);
		break;
	case AGL_SHELL_POLICY_EVENT_HIDE:
		/* FIXME: remove the active one, like basically unmap it? */
	default:
		break;
	}
}

static const struct ivi_policy_api policy_api = {
	.struct_size = sizeof(policy_api),
	.surface_create = ivi_policy_default_surface_create,
	.surface_commited = ivi_policy_default_surface_commmited,
	.surface_activate = ivi_policy_default_surface_activate,
	.policy_rule_allow_to_add = ivi_policy_default_allow_to_add,
	.policy_rule_try_event = ivi_policy_default_try_event,
};

int
ivi_policy_init(struct ivi_compositor *ivi)
{
	ivi->policy = ivi_policy_create(ivi, &policy_api, ivi);
	if (!ivi->policy)
		return -1;

	return 0;
}
