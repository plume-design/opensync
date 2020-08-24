/*
Copyright (c) 2020, Charter Communications Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
   3. Neither the name of the Charter Communications Inc. nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL Charter Communications Inc. BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef IOTM_TAG_PRIVATE_H_INCLUDED
#define IOTM_TAG_PRIVATE_H_INCLUDED

#include <inttypes.h>
#include <string.h>
#include "log.h"         /* Logging routines */
#include "iotm_tree.h"
/******************************************************************************
 * Misc Definitions
 *****************************************************************************/
#define TEMPLATE_VAR_CHAR       '$'

#define TEMPLATE_TAG_BEGIN      '{'
#define TEMPLATE_TAG_END        '}'

#define TEMPLATE_GROUP_BEGIN    '['
#define TEMPLATE_GROUP_END      ']'

#define TEMPLATE_DEVICE_CHAR    '@'
#define TEMPLATE_CLOUD_CHAR     '#'

#define OM_TLE_FLAG_DEVICE      (1 << 0)
#define OM_TLE_FLAG_CLOUD       (1 << 1)
#define OM_TLE_FLAG_GROUP       (1 << 2)


int tag_extract_vars(
				const char *rule,
				char var_chr,
                char begin,
				char end,
				uint8_t base_flags,
                iotm_list_t *vars);

#endif // IOTM_TAG_PRIVATE_H_INCLUDED */
