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

#ifndef OSW_DEFER_VIF_DOWN_H_INCLUDED
#define OSW_DEFER_VIF_DOWN_H_INCLUDED

struct osw_defer_vif_down;
struct osw_defer_vif_down_rule;
struct osw_defer_vif_down_observer;

typedef struct osw_defer_vif_down osw_defer_vif_down_t;
typedef struct osw_defer_vif_down_rule osw_defer_vif_down_rule_t;
typedef struct osw_defer_vif_down_observer osw_defer_vif_down_observer_t;

typedef void osw_defer_vif_down_notify_fn_t(void *fn_priv);

osw_defer_vif_down_rule_t *
osw_defer_vif_down_rule(osw_defer_vif_down_t *m,
                        const char *vif_name,
                        unsigned int grace_period_seconds);

void
osw_defer_vif_down_rule_free(osw_defer_vif_down_rule_t *r);

osw_defer_vif_down_observer_t *
osw_defer_vif_down_observer(osw_defer_vif_down_t *m,
                            const char *vif_name,
                            osw_defer_vif_down_notify_fn_t *grace_period_started_fn,
                            osw_defer_vif_down_notify_fn_t *grace_period_stopped_fn,
                            void *fn_priv);

void
osw_defer_vif_down_observer_free(osw_defer_vif_down_observer_t *o);

uint64_t
osw_defer_vif_down_get_remaining_nsec(osw_defer_vif_down_t *m,
                                         const char *vif_name);

#endif /* OSW_DEFER_VIF_DOWN_H_INCLUDED */
