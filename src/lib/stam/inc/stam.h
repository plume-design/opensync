/*
Copyright (c) 2015, Plume Design Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
   3. Neither the name of the Plume Design Inc. nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL Plume Design Inc. BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

/*
 * Sanity checks
 */
#if !defined(STAM_NAME)
#error STAM_NAME not defined. \
       See src/lib/stam/README.md for usage info.
#endif

#if !defined(STAM_STATES)
#error STAM_STATES not defined. \
       See src/lib/stam/README.md for usage info.
#endif

#if !defined(STAM_ACTIONS)
#error STAM_ACTIONS not defined. \
       See src/lib/stam/README.md for usage info.
#endif

#if !defined(STAM_GENERATE_HEADER) && !defined(STAM_GENERATE_SOURCE)
#error Either STAM_GENERATE_HEADER or STAM_GENERATE_SOURCE must be defined. \
       See src/liub/stam/README.md for usage info.
#endif

/*
 * Appending macros strings
 */
#define _STAM_CAT(x, y)             x ## y
#define STAM_CAT(x, y)              _STAM_CAT(x, y)
#define _STAM_STR(x)                #x
#define STAM_STR(x)                 _STAM_STR(x)

/* Append y to STAM_NAME */
#define STAM_NC(y)                  STAM_CAT(STAM_NAME, y)

/* Shortcut macros to make it a bit easier to read the code */
#define STAM_state_t                STAM_NC(_state_t)
#define STAM_state                  STAM_NC(_state)
#define STAM_state_str              STAM_NC(_state_str)
#define STAM_action                 STAM_NC(_action)
#define STAM_action_str             STAM_NC(_action_str)
#define STAM_state_call             STAM_NC(_state_call)
#define STAM_state_get              STAM_NC(_state_get)
#define STAM_state_prev             STAM_NC(_state_prev)
#define STAM_state_do               STAM_NC(_state_do)
#define STAM_STATE_EXCEPTION        STAM_NC(_EXCEPTION)
#define STAM_ACTION_STATE_INIT      STAM_NC(_do_STATE_INIT)
#define STAM_ACTION__END            STAM_NC(_do__END)
#define STAM_EXCEPTION__BEGIN       STAM_NC(_exception__BEGIN)
#define STAM_STATE__ERROR           STAM_NC(__ERROR)
#define STAM_STATE__BUSY            STAM_NC(__BUSY)
#define STAM_STATE__END             STAM_NC(__END)
#define STAM_name                   STAM_STR(STAM_NAME)
#define STAM_validate_transition    STAM_NC(_validate_transition)
#define STAM_validate_action        STAM_NC(_validate_action)

#if defined(STAM_GENERATE_HEADER)

/*
 * List of states
 */
#define STAM_STATE_ENUM(state)  \
    STAM_NC(_ ## state),

enum STAM_state {
    STAM_STATE__ERROR = INT8_MIN,
    STAM_STATE__BUSY = 0,
    STAM_STATES(STAM_STATE_ENUM)
#if defined(STAM_EXCEPTIONS)
    STAM_STATE_EXCEPTION,
#endif
    STAM_STATE__END
};

/*
 * List of actions
 */
#define STAM_ACTION_ENUM(act)  \
    STAM_NC(_do_ ## act),

#define STAM_EXCEPTION_ENUM(exc)  \
    STAM_NC(_exception_ ## exc),

enum STAM_action
{
    STAM_ACTION_STATE_INIT = 0,
    STAM_ACTIONS(STAM_ACTION_ENUM)
#if defined(STAM_EXCEPTIONS)
    STAM_EXCEPTION__BEGIN,
    STAM_EXCEPTIONS(STAM_EXCEPTION_ENUM)
#endif
    STAM_ACTION__END
};

typedef struct
{
    enum STAM_state state;
    enum STAM_state prev;
    uint32_t guard;
}
STAM_state_t;

/*
 * Create state handlers
 */

#define STAM_STATE_HANDLER(state)   \
    enum STAM_state STAM_NC(_state_ ## state)(STAM_state_t *stam, enum STAM_action action, void *data);

STAM_STATES(STAM_STATE_HANDLER)

#if defined(STAM_EXCEPTIONS)
enum STAM_state STAM_NC(_state_EXCEPTION)(STAM_state_t *stam, enum STAM_action action, void *data);
#endif

const char *STAM_action_str(enum STAM_action act);
const char *STAM_state_str(enum STAM_state state);

/*
 * Return current state
 */
enum STAM_state STAM_state_get(STAM_state_t *stam);

/*
 * Return previous state
 */
enum STAM_state STAM_state_prev(STAM_state_t *stam);

/*
 * Create the action function definition
 */
enum STAM_state STAM_state_do(STAM_state_t *stam, enum STAM_action action, void *data);

#if defined(STAM_transitions_checks)
bool STAM_validate_transition(enum STAM_state entry_state, enum STAM_state exit_state, enum STAM_action action);
#endif

#if defined(STAM_actions_checks)
bool STAM_validate_action(enum STAM_state entry_state, enum STAM_action action);
#endif

#include "stam_gen.h"

#endif /* STAM_GENERATE_HEADER */

#if defined(STAM_GENERATE_SOURCE)

/*
 * Debugs
 */
#if !defined(STAM_LOG)
#include "log.h"
#define STAM_LOG(...) LOG(TRACE, __VA_ARGS__)
#endif

#if !defined(STAM_WARN)
#include "log.h"
#define STAM_WARN(...) LOG(WARN, __VA_ARGS__)
#endif

/*
 * Descriptive array of actions
 */
#define STAM_ACTION_SWITCH_STR(action)  \
    case STAM_NC(_do_ ## action): return "do_" #action;

#define STAM_EXCEPTION_SWITCH_STR(action)  \
    case STAM_NC(_exception_ ## action): return "exception_" #action;

const char* STAM_action_str(enum STAM_action act)
{
    switch (act)
    {
        case STAM_ACTION__END:
            return "(end)";

        STAM_ACTION_SWITCH_STR(STATE_INIT)
        STAM_ACTIONS(STAM_ACTION_SWITCH_STR)
#if defined(STAM_EXCEPTIONS)
        case STAM_EXCEPTION__BEGIN:
            return "(exception begin)";

        STAM_EXCEPTIONS(STAM_EXCEPTION_SWITCH_STR)
#endif
    }

    return "do_?????";
}

#define STAM_STATE_SWITCH_STR(state)  \
    case STAM_NC(_ ## state): return #state;

const char* STAM_state_str(enum STAM_state state)
{
    switch (state)
    {
        case STAM_STATE__BUSY:
            return "(init)";
        case STAM_STATE__END:
            return "(end)";
        case STAM_STATE__ERROR:
            return "(error)";

        STAM_STATES(STAM_STATE_SWITCH_STR)
#if defined(STAM_EXCEPTIONS)
        STAM_STATE_SWITCH_STR(EXCEPTION)
#endif
    }

    return "?????";
}

/**
 *
 * @return true on success
 */
#define STAM_STATE_SWITCH(state)    \
    case STAM_NC(_ ## state):   \
        return STAM_NC(_state_ ## state)(stam, action, data);

enum STAM_state STAM_state_call(STAM_state_t *stam, enum STAM_state state, enum STAM_action action, void *data)
{
#if defined(LOG_H_INCLUDED)
    STAM_LOG("STAM: %s: [%s].%s", STAM_name, STAM_state_str(state), STAM_action_str(action));
#endif

    switch (state)
    {
        STAM_STATES(STAM_STATE_SWITCH)
#if defined(STAM_EXCEPTIONS)
        STAM_STATE_SWITCH(EXCEPTION)
#endif
        case STAM_STATE__ERROR:
        case STAM_STATE__BUSY:
        case STAM_STATE__END:
            return -1;
    }

    return -1;
}

enum STAM_state STAM_state_get(STAM_state_t *stam)
{
    return stam->state;
}

enum STAM_state STAM_state_prev(STAM_state_t *stam)
{
    return stam->prev;
}

enum STAM_state STAM_state_do(STAM_state_t *stam, enum STAM_action action, void *data)
{
    uint32_t cguard;

    enum STAM_state retval = 0;
    enum STAM_state next_state = 0;

    /* First call to STAM_state_do(), initialize the default state */
    if (stam->state == 0)
    {
        /*
         * Shift state to the first valid state (always number 1), all states implicitly
         * accepts STAM_ACTION_STATE_INIT action
         */
        stam->state = 1;
        next_state = STAM_state_do(stam, STAM_ACTION_STATE_INIT, NULL);
        /* In case an error occurred, stop processing here */
        if (next_state < 0)
        {
            return next_state;
        }
    }

    cguard = stam->guard;

    next_state = 0;
#if defined(STAM_EXCEPTIONS)
    /*
     * Fast transition to exception state
     */
    if (action > STAM_EXCEPTION__BEGIN)
    {
        next_state = STAM_STATE_EXCEPTION;
    }
#endif

    /*
     * Transition to a new state -- note that a transition to itself may
     * trigger a new transition
     */
    while (next_state >= 0)
    {
        if (next_state > 0)
        {
            STAM_LOG("STAM: %s: [%s -> %s] via %s",
                    STAM_name,
                    STAM_state_str(stam->state),
                    STAM_state_str(next_state),
                    STAM_action_str(action));

            stam->prev = stam->state;
            stam->state = next_state;
        }

#if defined(STAM_actions_checks)
#if defined(STAM_EXCEPTIONS)
        if (action < STAM_EXCEPTION__BEGIN)
#endif
        if (!STAM_validate_action(stam->state, action))
        {
            STAM_WARN("STAM: %s: Illegal action [%s at %s]",
                    STAM_name,
                    STAM_action_str(action),
                    STAM_state_str(stam->state));

            return STAM_STATE__ERROR;
        }
#endif
        /*
         * If the return code is 0 or error, do not touch the stam structure in
         * any way. It is allowed to free the current structure while returning
         * 0 or error. For this reason, remember the current state in case we
         * have to return it.
         */
        retval = stam->state;
        next_state = STAM_state_call(stam, stam->state, action, data);
        if (next_state == 0)
        {
            return retval;
        }
        else if (next_state < 0)
        {
            /*
             * In case of error just forward the error code returned from the
             * state handler
             */
            return next_state;
        }

        /*
         * Check for re-entrancy. It is allowed to call STAM_state_do()
         * recursively as long as a maximum of 1 call changes the state.
         */
        if (stam->guard != cguard)
        {
            STAM_WARN("STAM: %s: Re-entrancy detected. [%s(%s)]",
                    STAM_name,
                    STAM_state_str(stam->state),
                    STAM_action_str(action));

            return STAM_STATE__ERROR;
        }
        cguard = ++stam->guard;

#if defined(STAM_transitions_checks)
        if (!STAM_validate_transition(stam->state, next_state, action))
        {
            STAM_WARN("STAM: %s: Illegal transition [%s(%s) -> %s]",
                    STAM_name,
                    STAM_state_str(stam->state),
                    STAM_action_str(action),
                    STAM_state_str(next_state));

            return STAM_STATE__ERROR;
        }
#endif
        /* Transition successful, the next action is always STATE_INIT */
        action = STAM_ACTION_STATE_INIT;
    }

    /*
     * The code above should not be able to reach this point. Future proof it
     * by showing an error message.
     */
    STAM_WARN("STAM: %s: Internal error [%s at %s]",
            STAM_name,
            STAM_action_str(action),
            STAM_state_str(stam->state));

    return STAM_STATE__ERROR;
}

#endif

#undef STAM_state_t
#undef STAM_state
#undef STAM_state_str
#undef STAM_action
#undef STAM_action_str
#undef STAM_state_call
#undef STAM_state_get
#undef STAM_state_prev
#undef STAM_state_do
#undef STAM_STATE_EXCEPTION
#undef STAM_ACTION_STATE_INIT
#undef STAM_ACTION__END
#undef STAM_validate_transition
#undef STAM_validate_action
#undef STAM_STATE__ERROR
#undef STAM_STATE__BUSY
#undef STAM_STATE__END

#undef STAM_GENERATE_HEADER
#undef STAM_GENERATE_SOURCE

#undef STAM_NAME
#undef STAM_STATES
#undef STAM_ACTIONS
#undef STAM_EXCEPTIONS

