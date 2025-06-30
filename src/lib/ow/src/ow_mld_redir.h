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

#ifndef OW_MLD_REDIR_H_INCLUDED
#define OW_MLD_REDIR_H_INCLUDED

typedef struct ow_mld_redir ow_mld_redir_t;
typedef struct ow_mld_redir_observer ow_mld_redir_observer_t;

typedef void ow_mld_redir_changed_fn_t(void *priv, const char *mld_if_name, const char *vif_redir, char **vifs);

ow_mld_redir_observer_t *ow_mld_redir_observer_alloc(ow_mld_redir_t *m, ow_mld_redir_changed_fn_t *fn, void *priv);
const char *ow_mld_redir_get_mld_redir_vif_name(ow_mld_redir_t *m, const char *mld_name);
void ow_mld_redir_observer_drop(ow_mld_redir_observer_t *o);

#endif /* OW_MLD_REDIR_H_INCLUDED */
