#include <string.h>
#include <libweston/zalloc.h>
#include <assert.h>

#include "shared/helpers.h"
#include "ivi-compositor.h"

#include "policy.h"
#include "agl-shell-policy-server-protocol.h"

static struct wl_global *
ivi_policy_proto_create(struct ivi_compositor *ivi, struct ivi_policy *policy);

static void
ivi_policy_remove_state_event(struct state_event *st_ev)
{
	free(st_ev->name);
	wl_list_remove(&st_ev->link);
	free(st_ev);
}

static void
ivi_policy_destroy_state_event(struct wl_list *list)
{
	struct state_event *st_ev, *tmp_st_ev;
	wl_list_for_each_safe(st_ev, tmp_st_ev, list, link)
		ivi_policy_remove_state_event(st_ev);
}

static struct state_event *
ivi_policy_state_event_create(uint32_t val, const char *value)
{
	struct state_event *ev_st = zalloc(sizeof(*ev_st));
	size_t value_len = strlen(value);

	ev_st->value = val;
	ev_st->name = zalloc(sizeof(char) * value_len + 1);
	memcpy(ev_st->name, value, value_len);

	return ev_st;
}

static void
ivi_policy_add_state(struct wl_client *client,
		     struct wl_resource *res, uint32_t state, const char *value)
{
	struct ivi_policy *policy = wl_resource_get_user_data(res);
	struct state_event *ev_st = ivi_policy_state_event_create(state, value);
	wl_list_insert(&policy->states, &ev_st->link);
}

static void
ivi_policy_add_event(struct wl_client *client,
		     struct wl_resource *res, uint32_t ev, const char *value)
{
	struct ivi_policy *policy = wl_resource_get_user_data(res);
	struct state_event *ev_st = ivi_policy_state_event_create(ev, value);
	wl_list_insert(&policy->events, &ev_st->link);
}

/* note, if protocol modifications, adapt here as well */
static void
ivi_policy_add_default_states(struct ivi_policy *policy)
{
	const char *default_states[] = { "invalid", "start", "stop", "reverse" };
	for (uint32_t i = 0; i < ARRAY_LENGTH(default_states); i ++) {
		struct state_event *ev_st =
			ivi_policy_state_event_create(i, default_states[i]);
		wl_list_insert(&policy->states, &ev_st->link);
	}
}

/* note, if protocol modifications, adapt here as well */
static void
ivi_policy_add_default_events(struct ivi_policy *policy)
{
	const char *default_events[] = { "show", "hide" };
	for (uint32_t i = 0; i < ARRAY_LENGTH(default_events); i ++) {
		struct state_event *ev_st =
			ivi_policy_state_event_create(i, default_events[i]);
		wl_list_insert(&policy->events, &ev_st->link);
	}
}

static void
ivi_policy_try_event(struct ivi_a_policy *a_policy)
{
	struct ivi_policy *policy = a_policy->policy;

	if (policy->api.policy_rule_try_event)
	    return policy->api.policy_rule_try_event(a_policy);
}

static int
ivi_policy_try_event_timeout(void *user_data)
{
	struct ivi_a_policy *a_policy = user_data;
	ivi_policy_try_event(a_policy);
	return 0;
}

static void
ivi_policy_setup_event_timeout(struct ivi_policy *ivi_policy,
			       struct ivi_a_policy *a_policy)
{
	struct ivi_compositor *ivi = ivi_policy->ivi;
	struct wl_display *wl_display = ivi->compositor->wl_display;
	struct wl_event_loop *loop = wl_display_get_event_loop(wl_display);

	a_policy->timer = wl_event_loop_add_timer(loop,
						  ivi_policy_try_event_timeout,
						  a_policy);

	wl_event_source_timer_update(a_policy->timer, a_policy->timeout);
}

static void
ivi_policy_check_policies(struct wl_listener *listener, void *data)
{
	struct ivi_a_policy *a_policy;
	struct ivi_policy *ivi_policy =
		wl_container_of(listener, ivi_policy, listener_check_policies);

	ivi_policy->state_change_in_progress = true;
	wl_list_for_each(a_policy, &ivi_policy->policies, link) {
		if (ivi_policy->current_state == a_policy->state) {
			/* check the timeout first to see if there's a timeout */
			if (a_policy->timeout > 0)
				ivi_policy_setup_event_timeout(ivi_policy,
							       a_policy);
			else
				ivi_policy_try_event(a_policy);
		}
	}

	ivi_policy->previous_state = ivi_policy->current_state;
	ivi_policy->state_change_in_progress = false;
	agl_shell_policy_send_done(ivi_policy->resource,
				   ivi_policy->current_state);
}

/*
 * The generic way would be the following:
 *
 * - 'car' is in 'state' ->
 *   	{ do 'event' for app 'app_id' at 'timeout' time if same state as 'car_state' }
 *
 * a 0 timeout means, immediately, a timeout > 0, means to install timer an
 * execute when timeout expires
 *
 * The following happens:
 * 'car' changes its state -> verify what policy needs to be run
 * 'car' in same state -> no action
 *
 */
struct ivi_policy *
ivi_policy_create(struct ivi_compositor *ivi,
                  const struct ivi_policy_api *api, void *user_data)
{
	struct ivi_policy *policy = zalloc(sizeof(*policy));

	policy->user_data = user_data;
	policy->ivi = ivi;
	policy->state_change_in_progress = false;

	policy->api.struct_size =
		MIN(sizeof(struct ivi_policy_api), api->struct_size);
	memcpy(&policy->api, api, policy->api.struct_size);

	policy->policy_shell = ivi_policy_proto_create(ivi, policy);
	if (!policy->policy_shell) {
		free(policy);
		return NULL;
	}

	wl_signal_init(&policy->signal_state_change);

	policy->listener_check_policies.notify = ivi_policy_check_policies;
	wl_signal_add(&policy->signal_state_change,
		      &policy->listener_check_policies);

	policy->current_state = AGL_SHELL_POLICY_STATE_INVALID;
	policy->previous_state = AGL_SHELL_POLICY_STATE_INVALID;

	wl_list_init(&policy->policies);
	wl_list_init(&policy->events);
	wl_list_init(&policy->states);

	/* add the default states and enums */
	ivi_policy_add_default_states(policy);
	ivi_policy_add_default_events(policy);

	return policy;
}

void
ivi_policy_destroy(struct ivi_policy *ivi_policy)
{
	struct ivi_a_policy *a_policy, *a_policy_tmp;

	if (!ivi_policy)
		return;

	wl_list_for_each_safe(a_policy, a_policy_tmp,
			      &ivi_policy->policies, link) {
		free(a_policy->app_id);
		wl_list_remove(&a_policy->link);
		free(a_policy);
	}

	ivi_policy_destroy_state_event(&ivi_policy->states);
	ivi_policy_destroy_state_event(&ivi_policy->events);

	if (ivi_policy->policy_shell)
		wl_global_destroy(ivi_policy->policy_shell);

	free(ivi_policy);
}

/* verifies if the state is one has been added */
static bool
ivi_policy_state_is_known(uint32_t state, struct ivi_policy *policy)
{
	struct state_event *ev_st;

	wl_list_for_each(ev_st, &policy->states, link) {
		if (ev_st->value == state) {
			return true;
		}
	}

	return false;
}

static void
ivi_policy_add(struct wl_client *client, struct wl_resource *res,
	       const char *app_id, uint32_t state, uint32_t event,
	       uint32_t timeout, struct wl_resource *output_res)
{
	size_t app_id_len;
	struct weston_head *head = weston_head_from_resource(output_res);
	struct weston_output *woutput = weston_head_get_output(head);
	struct ivi_a_policy *c_policy = zalloc(sizeof(*c_policy));

	struct ivi_output *output = to_ivi_output(woutput);
	struct ivi_policy *policy = wl_resource_get_user_data(res);

	assert(!policy);

	if (policy->state_change_in_progress) {
		weston_log("State change in progress\n");
		wl_resource_post_event(res,
				       AGL_SHELL_POLICY_ERROR_POLICY_STATE_CHANGE_IN_PROGRESS,
				       "State change in progress");
		return;
	}
	
	/* we should be allow to do this in the first place, only if the
	 * hooks allows us to  */
	if (policy->api.policy_rule_allow_to_add &&
	    !policy->api.policy_rule_allow_to_add(policy)) {
		wl_resource_post_event(res,
				       AGL_SHELL_POLICY_ERROR_POLICY_NOT_ALLOWED,
				       "Not allow to add policy");
		return;
	}

	if (!ivi_policy_state_is_known(state, policy)) {
		wl_resource_post_event(res,
				       AGL_SHELL_POLICY_ERROR_POLICY_STATE_UNKNOWN,
				       "State is not known, please add it");
		return;
	}

	c_policy = zalloc(sizeof(*c_policy));

	app_id_len = strlen(app_id);
	c_policy->app_id = zalloc(sizeof(char) * app_id_len + 1);
	memcpy(c_policy->app_id, app_id, app_id_len);

	c_policy->state = state;
	c_policy->event = event;
	c_policy->timeout = timeout;
	c_policy->output = output;
	c_policy->policy = policy;

	wl_list_insert(&policy->policies, &c_policy->link);
}

/* we start with 'invalid' state, so a initial state to even 'stop' should
 * trigger a check of policies
 */
static void
ivi_policy_state_change(struct wl_client *client, struct wl_resource *res,
			uint32_t state)
{
	struct ivi_policy *policy = wl_resource_get_user_data(res);
	bool found_state = false;

	if (!policy) {
		weston_log("Failed to retrieve policy!\n");
		return;
	}

	/* FIXME: should send here AGL_SHELL_POLICY_STATE_INVALID? */
	if (policy->current_state == state) {
		/* send done with the same state value back */
		agl_shell_policy_send_done(policy->resource,
					   policy->current_state);
		return;
	}

	/* if we don't know the state, make sure it is first added */
	found_state = ivi_policy_state_is_known(state, policy);
	if (!found_state) {
		agl_shell_policy_send_done(policy->resource,
					   AGL_SHELL_POLICY_STATE_INVALID);
		return;
	}

	/* current_state is actually the new state */
	policy->current_state = state;

	/* signal that we need to check the current policies */
	wl_signal_emit(&policy->signal_state_change, policy);
}

static const struct agl_shell_policy_interface ivi_policy_interface = {
	ivi_policy_add_state,
	ivi_policy_add_event,
	ivi_policy_add,
	ivi_policy_state_change,
};

static void
ivi_policy_bind(struct wl_client *client, void *data,
		uint32_t version, uint32_t id)
{
	struct ivi_policy *ivi_policy = data;	
	struct wl_resource *resource;

	resource = wl_resource_create(client, &agl_shell_policy_interface,
				      version, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(resource, &ivi_policy_interface,
				       ivi_policy, NULL);
	ivi_policy->resource = resource;
}

static struct wl_global *
ivi_policy_proto_create(struct ivi_compositor *ivi, struct ivi_policy *policy)
{
	struct wl_global *policy_global = NULL;

	if (ivi->policy)
		return NULL;

	policy_global = wl_global_create(ivi->compositor->wl_display,
					 &agl_shell_policy_interface, 1,
					 policy, ivi_policy_bind);

	return policy_global;
}
