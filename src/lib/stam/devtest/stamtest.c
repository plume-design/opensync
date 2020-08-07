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

#include <stdio.h>
#include <assert.h>

#include "log.h"

#include "test_stam.h"

#define FAIL(...)                           \
    do                                      \
    {                                       \
        LOG(ERR, "[FAIL] " __VA_ARGS__);     \
        exit(1);                            \
    }                                       \
    while (0)

#define PASS(...)                           \
    do                                      \
    {                                       \
        LOG(NOTICE, "[PASS] " __VA_ARGS__); \
    }                                       \
    while (0)


enum test_state test_state_INIT(test_state_t *state, enum test_action action, void *data)
{
    (void)state;
    (void)action;
    (void)data;

    if (action == test_do_STATE_INIT)
    {
        PASS("STATE_INIT in INIT state.\n");
        return test_RETRANSITION;
    }

    FAIL("INIT state received action: %s", test_action_str(action));

    return 0;
}

enum test_state test_state_RETRANSITION(test_state_t *state, enum test_action action, void *data)
{
    static int count = 0;

    if (action != test_do_STATE_INIT)
    {
        FAIL("RETRANSIITON state received invalid action: %s", test_action_str(action));
        return -1;
    }

    if (count++ >= 2)
    {
        FAIL("RETRANSITION state has too many retransitions\n");
    }

    if (test_state_prev(state) == test_INIT)
    {
        return test_RETRANSITION;
    }
    else if (test_state_prev(state) == test_RETRANSITION)
    {
        PASS("Retransition success\n");
        PASS("This also means that STAM_state_prev() works.\n");
        return test_RUNNING;
    }

    return 0;
}

enum test_state test_state_RUNNING(test_state_t *state, enum test_action action, void *data)
{
    (void)state;
    (void)action;
    (void)data;

    printf("RUNNING: %s\n", test_action_str(action));

    return test_STOPPED;
}

enum test_state test_state_STOPPED(test_state_t *state, enum test_action action, void *data)
{
    (void)state;
    (void)action;
    (void)data;

    printf("STOPPED: %s\n", test_action_str(action));

    return 0;
}

enum test_state test_state_EXCEPTION(test_state_t *state, enum test_action action, void *data)
{
    switch (action)
    {
        case test_do_STATE_INIT:
            FAIL("EXCEPTION state never calls STATE_INIT");
            break;

        case test_exception_TIMEOUT:
            return test_STOPPED;

        default:
            /* Invalid transition */
            return test_RUNNING;
    }

    return 0;
}

int main(void)
{
    test_state_t test = { 0 };

    log_open("STAM", LOG_OPEN_STDOUT);

    if (test_state_do(&test, test_do_START, NULL) < 0)
    {
        LOG(ERR, "State machine error");
    }

    if (test_state_do(&test, test_exception_TIMEOUT, NULL) < 0)
    {
        LOG(ERR, "Error executing exception");
    }

    printf("CANCELLING...\n");
    if (test_state_do(&test, test_exception_CANCEL, NULL) < 0)
    {
        LOG(ERR, "Error executing exception");
    }

    return 0;
}
