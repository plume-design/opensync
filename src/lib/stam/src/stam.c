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

/* Define dummy state machine just to get common declarations */
#include <stdbool.h>
#include <stddef.h>

#include "stam_gen.h"

bool stam_validate_relation(int entry_state, int exit_state, int action, const struct stam_trans* valid_transitions, int state_terminator, int action_terminator)
{
    const struct stam_trans *transition;
    const int *valid_action;

    for (transition = valid_transitions; transition->entry_state != state_terminator; transition++) {
        if (transition->entry_state != entry_state)
            continue;

        /* Check exit_state only if it was specified */
        if (exit_state != state_terminator)
        {
            if (transition->exit_state != exit_state)
                continue;
        }

        /* No actions were assigned to this transition */
        if (!transition->valid_actions)
            return true;

        for (valid_action = transition->valid_actions; *valid_action != action_terminator; valid_action++)
        {
            if (*valid_action == action)
                return true;
        }
    }

    return false;
}
