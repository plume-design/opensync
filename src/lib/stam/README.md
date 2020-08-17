# State Machine Library (libstam)

`libstam` is a finite state machine library. The majority of the library is
implemented as a header file using
[Xmacros](https://en.wikipedia.org/wiki/X_Macro).

See the [Example](#example) below.

## Design Principles

Each state machine using `libstam` must define a set of states and a set of
actions. Optionally it can define a set of exceptions. Using this information,
`libstam` will automatically create the following code for you:

  - State structure
  - State and exception enums and associated helper functions
  - State machine functions (action and state switch/dispatcher)
  - State handlers prototypes (the actual implementation must be provided by
    the user)

### Code and header generation

Before including the `libstam` header file, the user of the library must define
the following macros:

  - `STAM_NAME` to define the state machine name; this is used to prefix most
    of the generate C symbols
  - `STAM_STATES` to define all possible state machine states, this is an Xmacro
    list
  - `STAM_ACTIONS` to define all possible actions (or messages), this is an
     Xmacro list

And optionally:
  - `STAM_EXCEPTIONS` is used to define possible actions that force a fast
    switch to the `EXCEPTION` state. If this macro is defined, the `EXCEPTION`
    state will be implicitly generated. However, the user must still implement
    a state handler.

Using the `STAM_NAME`, `STAM_STATES` and `STAM_ACTIONS` user-defined macros,
`libstam` will automatically generate the state structure and enums describing
all states and actions.

Additionally, the user must select what kind of source code must be generated
by using the following two macros:

  - `STAM_GENERATE_HEADER` will generate only code that should be present in
    header files
  - `STAM_GENERATE_SOURCE` will generate the code body

#### States Enum Generation

If STAM_GENERATE_HEADER is defined, for each state defined in `STAM_STATES`, a C
symbol will be generated as follows:

  - Concatenate `STAM_NAME` and append `_state` to generate the enum name
  - Concatenate `STAM_NAME` and append `_` and the state name to generate an enum
    entry
  - Concatenate `STAM_NAME` and append `_state_str` to generate the helper
    function for converting state constants to strings

Using the [example](#example) below, the following code will be generated:

    enum foo_state
    {
        foo_INIT,
        foo_IDLE,
        foo_RUNNING
    };

    const char *foo_state_str(enum foo_state state);

_Note: If `STAM_EXCEPTIONS` is defined, one additional state is implicitly
generated: `foo_EXCEPTION`._

#### Actions Enum Generation

If STAM_GENERATE_HEADER is defined, for each action defined in `STAM_ACTION`, a C
symbol will be generated as follows:

  - Concatenate `STAM_NAME` and append `_action` to generate the enum name
  - Concatenate `STAM_NAME` and append `_do_STATE_INIT` to generate
    an implicit `STATE_INIT` action
  - Concatenate `STAM_NAME` and append `_do_` and the action name to generate an
    enum entry
  - Concatenate `STAM_NAME` and append `_action_str` to generate the helper
    function for converting state constants to strings

Using the [example](#example) below, the following code will be generated:

    enum foo_action
    {
        foo_do_STATE_INIT = 0,  /* Implicitly generated */
        foo_do_RUN,
        foo_do_STOP,
        foo_do_EXIT
    };

    const char *foo_action_str(enum foo_state state);

##### STATE_INIT Action

`foo_do_STATE_INIT` is implicitly generated. The action is issued to the state
handler when the state machine switches to the new state, which can be used to
initialize the state as this action is called exactly once. The STATE_INIT
action is execute for each normal state transitions.

An exception to this is the EXCEPTION state. Exceptions break the current flow
of the state machine and fast transition to the EXCEPTION state, without sending
the STATE_INIT action first. The EXCEPTION state transitions to other states
normally.

#### State Structure Generation

If `STAM_GENERATE_HEADER` is defined, single structure will be generated that
will be used for state tracking. The structure is generated as follows:

  - Conctenate `STAM_NAME` and append `_state_t`, this will be the name of the
    typedeffed anonymous structure.

#### State Machine Function Generation

If `STAM_GENERATE_SOURCE` is defined, state switch and state disaptcher functions
(switch-case to translate the states enum to function calls) will be generated
according to the following rules:

  - Use `STAM_NAME` and append `_state_do` to generate the main state switch
    function.
  - Use `STAM_NAME` and append `_state_get` to generate the function to
    return the current state.
  - Use `STAM_NAME` and append `_state_prev` to generate the function to
    return the previous state.

The `_state_do` is the state switch/dispatcher function. The state machine
itself should be considered a black-box and `_state_do` should be used to send
actions to it. `_state_do` will return the current state or a negative number
if an error occurred.

The `_state_get` function is used to retrieve the current state of the state
machine.

The pseudo code that will be generated for [example](#example):

    enum foo_state foo_state_do(foo_state_t *state, enum foo_action, void *data);
    enum foo_state foo_state_get(foo_state_t *state);

Example code usage (send the action STOP to the state machine):

    foo_state_do(&state, FOO_do_STOP, NULL);

### State Handlers

Each state must have a corresponding state handler. `libstam` takes care of
generating prototypes and dispatcher functions for state handlers, however, the
state handler implementation must be provided by the user.

The state handler C symbol name is generated according to the following rules:

  - Use `STAM_NAME` and append `_state_` and the state name

Using the [example](#example) below, the following prototypes will be generated:

    enum foo_state foo_state_INIT(foo_state_t *state, enum foo_action action, void *data);
    enum foo_state foo_state_IDLE(foo_state_t *state, enum foo_action action, void *data);
    enum foo_state foo_state_RUNNING(foo_state_t *state, enum foo_action action, void *data);

The first call to the state handler will always be the `STATE_INIT` action (with
the exception of the EXCEPTION state, which executes the action immediately).
This is used as state initializer. Otherwise the action will be the one supplied
by the `_state_do()` function.

State handlers must react to actions and will be invoked by the `_state_do()`
function. The state handlers are also able to switch states by returning the
state they wish to switch to. The return value is interpreted as follows:

  - 0 or the current state -- this informs `_state_do()` that the state is
    currently busy and is waiting for an external event or action
  - A new state that the state machine must switch to (`_state_do()` will be
    responsible for turning the wheels and, for example, sending the `STATE_INIT`
    action first)
  - Returning the current state will cause a re-transition to itself
  - Negative values are treated as errors and `_state_do()` returns immediately
    by forwarding the negative value to the caller

## Usage

The `_state_t` structure must be initialized to 0 (memset or otherwise). The
state machine will be initialized by default to the first state defined in the
`STAM_STATES` macro. The state machine should be considered a black box -- it can
only accept actions, not actual forced state switches. It is up to the state
handlers to react to the requested action and switch to the desired state.

The main API for sending actions to the state machine is the `_state_do()`
function. This function will loop and dispatch the actions to the current state
handler until the state handler either returns 0 or the current state. This
indicates that the current state is busy waiting for an external event/action.

# Example

This is a simple example of the `libstam` usage. It is used throughout
this README as reference.

    #include <stdio.h>
    #include <readline/readline.h>

    /*
     * Define the state machine name; the name will be prepended to the
     * generated C symbols
     */
    #define STAM_NAME   foo

    /* Define the list of possible states */
    #define STAM_STATES(state)      \
        state(INIT)                 \
        state(IDLE)                 \
        state(RUNNING)

    /* Define list of actions */
    #define STAM_ACTIONS(action)    \
        action(START)               \
        action(STOP)                \
        action(EXIT)

    /*
     * Define list of exceptions -- exceptions are actions that fast switch
     * to the EXCEPTION state. If exceptions are defined, an EXCEPTION state
     * will be implicitly generated.
     */

    /* Generate headers */
    #define STAM_GENERATE_HEADER

    /* Generate soruce */
    #define STAM_GENERATE_SOURCE

    /* Debugging function */
    #define STAM_LOG(...) do { printf(__VA_ARGS__); printf("\n"); } while (0)

    #include "inc/stam.h"

    enum foo_state foo_state_INIT(foo_state_t *state, enum foo_action act, void *data)
    {
        (void)state;
        (void)data;

        printf("INIT state received %s.\n", foo_action_str(act));

        switch (act)
        {
            case foo_do_START:
                return foo_RUNNING;

            case foo_do_STOP:
                return foo_IDLE;

            case foo_do_EXIT:
                printf("Warning: foo was never started.\n");
                exit(1);

            default:
                break;
        }

        return 0;
    }

    enum foo_state foo_state_RUNNING(foo_state_t *state, enum foo_action act, void *data)
    {
        (void)state;
        (void)data;

        printf("RUNNING state received %s.\n", foo_action_str(act));

        switch (act)
        {
            case foo_do_STATE_INIT:
                printf("STARTING SERVICE\n");
                break;

            case foo_do_START:
                printf("Already running, nothing to do.\n");
                break;

            case foo_do_STOP:
                return foo_IDLE;

            case foo_do_EXIT:
                printf("Service must be stopped first.\n");
                break;
        }
        return 0;
    }

    enum foo_state foo_state_IDLE(foo_state_t *state, enum foo_action act, void *data)
    {
        (void)state;
        (void)data;

        printf("IDLE state received %s.\n", foo_action_str(act));

        switch (act)
        {
            case foo_do_STATE_INIT:
                /*
                 * The state switches after STATE_INIT is processed; we can
                 * inspect the previous state here.
                 */
                if (foo_state_get(state) != foo_INIT)
                {
                    printf("STOPPING SERVICE\n");
                }
                break;

            case foo_do_START:
                return foo_RUNNING;

            case foo_do_STOP:
                printf("Service already stopped.\n");
                break;

            case foo_do_EXIT:
                printf("Exiting...\n");
                exit(1);
        }
        return 0;
    }

    int main(void)
    {
        char *rl;

        foo_state_t state = {0};

        printf("Available commands: start stop exit\n\n");

        while ((rl = readline("> ")) != NULL)
        {
            if (strcasecmp(rl, "start") == 0)
            {
                printf("Action START\n");
                foo_state_do(&state, foo_do_START, NULL);
            }
            else if (strcasecmp(rl, "stop") == 0)
            {
                printf("Action STOP\n");
                foo_state_do(&state, foo_do_STOP, NULL);
            }
            else if (strcasecmp(rl, "exit") == 0)
            {
                printf("Action EXIT\n");
                foo_state_do(&state, foo_do_EXIT, NULL);
            }
            else
            {
                printf("Unknown command: %s\n", rl);
            }

            free(rl);
        }
    }

_Warning: libreadline is required for the example to compile._

## Code generation
STAM comes with optional code generator capable of genertaing C source code for state machines defined in DOT format. Here's sample executable unit called `foo` showing how to integrate STAM + code generation with OpenSync build system.

The code generstor is written in Python 3 and requires [PyDot](https://pypi.org/project/pydot/) package.

Unit structure:
```
$ tree src/foo/
src/foo/
├── foo.dot
├── src
│   └── foo.c
└── unit.mk
```

`foo.dot`:
```
digraph {
    a -> b [label="START"];
    a -> b [label="QUICK_START"];
    a -> d [label="STOP"];
    b -> b [label="IDLE"];
    b -> c [label="DO_STH"],
    c -> d [label="STOP"];
    d -> a;
}
```
To define the default (initial) state, a node has to be defined with the `init`
attribute set. For example:

```
digraph {
    a[init=true];
    ...
}
```

Exceptions are defined by prefixing the action name with `!`. Exceptions are
legal only when transition to or from the `EXCEPTION` state. When the `EXCEPTION`
state is defined in the .dot file, the `STAM_EXCEPTIONS` macro will be
automatically generated. Any state transitioning to the `EXCEPTION` state is
ignored -- legally any state can transition to `EXCEPTION`, so the
`X -> EXCEPTION` is used only to generate the exception list. The node `X`
however may be used to describe in more detail the exception (the mechanic, why
it happens, etc.).

The example below defines a `TIMEOUT` and a `HARD_RESET` exception. The
`EXCEPTION` state can transition immediately to the `INIT` state upon receiving
the `HARD_RESET` exception. However, it will remain in the `EXCEPTION` state when
a `TIMEOUT` exception has occurred. To recover from this situation, a `RESTART`
action must be issued.

digraph {
    ....

    EX_TIMEOUT[label="The TIMEOUT exceptions occurrs after 30 seconds."];
    EX_TIMEOUT -> EXCEPTION [label="!TIMEOUT"];
    EX_HARD_RESET[label="Forcibly restart the sequence."];
    EX_HARD_RESET -> EXCEPTION [label="!HARD_RESET"];

    EXCEPTION -> INIT [label="RESTART"];
    EXCEPTION -> INIT [label="!HARD_RESET"];
    ....
}

Only exceptions can transition to the `EXCEPTION` state. However, the `EXCEPTION`
state can transition to other states using exceptions OR actions.

`foo.c`:
```
#include <stdio.h>

#define LOG(x, ... ) printf(__VA_ARGS__);printf("\n")

#define STAM_GENERATE_SOURCE
#define STAM_GENERATE_HEADER
#include "foo_gen.h"
#include "stam.h"

enum foo_state foo_state_A(foo_state_t *state, enum foo_action act, void *data) {
    if (act == foo_do_STATE_INIT)
        return 0;
    else if (act == foo_do_START)
        return foo_B;
    else
        return -1;
}
enum foo_state foo_state_B(foo_state_t *state, enum foo_action act, void *data) {
    if (act == foo_do_DO_STH)
        return foo_A; // This is expected to fail
    else
        return foo_B; // This is expected to fail
}
enum foo_state foo_state_C(foo_state_t *state, enum foo_action act, void *data) {
    return foo_B;
}
enum foo_state foo_state_D(foo_state_t *state, enum foo_action act, void *data) {
    return foo_D;
}

int main()
{
    foo_state_t state = {0};
    foo_state_do(&state, foo_do_START, NULL);
    foo_state_do(&state, foo_do_START, NULL);
    foo_state_do(&state, foo_do_STOP, NULL); // This is expected to fail
    foo_state_do(&state, foo_do_DO_STH, NULL);
}
```

The STAM generator fully supports unit.mk files and adding a state machine to
your project is just a matter of calling `stam_generate` make template:

```
UNIT_NAME := foo
UNIT_TYPE := BIN

UNIT_CFLAGS := -I$(UNIT_BUILD)
UNIT_CFLAGS += -I$(TOP_DIR)/src/lib/stam/inc/

UNIT_SRC := src/foo.c

# Generate state machine foo
$(eval $(call stam_generate, foo.dot))

```

