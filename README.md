# agl-compositor

## Policy

The compositor contains an API useful for defining policy rules.  It contains
the bare minimum and installs, by default, an allow-all kind of engine.

Users wanted to create their own policy engine should create a specialized
version and use `struct ivi_policy_api` where they can install their own
callbacks.

The default policy found in src/policy-default.c should more than sufficient to
get started. Users can either re-puporse the default policy or create a new one
entirely different, based on their needs.

### Hooks

These are hooks for allowing creation, committing and activation of surfaces
(and these are ivi_policy_api::surface_create, ivi_policy_api::surface_commited,
ivi_policy_api::surface_activate).

Another hook, ivi_policy_api::policy_rule_allow_to_add can be used to control
if policy rules (the next type) can be added or not. Finally, we have
ivi_policy_api::policy_rule_try_event which is executed in context of policy
rules.

The current, installed hooks, permit all the above, but users can customize
this behaviour by using some sort of database to retrieve the application name
to compare against, or incorporate some kind of policy rule engine.


### Policy rules

Policy (injection) rules can be added using the agl-shell-policy protocol.  The
protocol allows to define policy rules that should be executed by using the
ivi_policy_api::policy_rule_try_event callback. These are particularly useful
when handling state changes. The protocol supports adding new states and events
and the default implementation has code for handling events like showing or
hiding the application specified in the policy rule. The protocol XML file on
contains a few more details than written here.

ivi_policy_api::policy_rule_try_event callback should be adapted and correlated
with the client shell in case additional events and states are added.

By default the server side implementations adds the 'show', and 'hide' events
and the 'start', 'stop' and 'reverse' states. An special type, assigned by
default is 'invalid'. A state change has to be propaged from the client to the
compositor to signal the compositor the change itself, in order to apply the
policy rules.
