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

/*
 * Constant Helpers
 */

#ifndef CONST_H_INCLUDED
#define CONST_H_INCLUDED

#include <stdint.h>
#include <stdbool.h>

/*
 * ===========================================================================
 *  String constants
 *  _LEN - refers to string length, the length must include the terminating \0 character
 *  _SZ  - refers to sizes of stuff, as returned by sizeof()
 *  _MAX - refers to a maximum value of a numeric value, or maximum array length (for example, maximum number of interfaces)
 *  _MIN - refers to a minimum value of a numeric value
 * ===========================================================================
 */
#define C_HOSTNAME_LEN  64                                  /**< Maximum hostname name (without FQDN) */
#define C_FQDN_LEN      256                                 /**< Fully qualified hostname maximum length */
#define C_IFNAME_LEN    32                                  /**< Interface name size, including trailing \0 */
#define C_MACADDR_LEN   sizeof("11:22:33:44:55:66")         /**< Maximum length of a MAC address represented as string */
#define C_IPADDR_LEN    sizeof("255.255.255.255")           /**< Maximum length of an IP address represented as string */
#define C_MAXPATH_LEN   256                                 /**< Shorter than MAXPATH, but sufficient to access any path on the device */
#define C_WPA_PSK_LEN   64                                  /**< WPA* PSK maximum length */

/*
 * ===========================================================================
 *  Various system constants
 * ===========================================================================
 */
#define C_INVALID_PID   -1

/*
 * ===========================================================================
 *  Constant array/table handling
 * ===========================================================================
 */

// Return container pointer, given a member's pointer
#ifndef container_of
#define container_of(ptr, type, member) \
                            ((type *)((char *)ptr - offsetof(type, member)))
#endif /* container_of */

// Return number of elements in array
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x)       (sizeof(x) / sizeof(x[0]))
#endif /* ARRAY_SIZE */

// Same as ARRAY_SIZE, except returned signed value
#ifndef ARRAY_LEN
#define ARRAY_LEN(x)  ((int)ARRAY_SIZE(x))
#endif /* ARRAY_LEN */

#ifndef ARRAY_AND_SIZE
#define ARRAY_AND_SIZE(x)   (x),ARRAY_SIZE(x)
#endif /* ARRAY_AND_SIZE */

typedef struct {
    int32_t         value;
    int32_t         param;
    intptr_t        key;
    intptr_t        data;
} c_item_t;

#define C_ITEM_CB(key, cb)          {0, 0, (int)key, (intptr_t)cb}
#define C_ITEM_VAL(key, val)        {val, 0, (int)key, 0}
#define C_ITEM_VAL_PARAM(key, v, p) {v, p, (int)key, 0}
#define C_ITEM_STR(key, str)        {0, sizeof(str)-1, (int)key, (intptr_t)str}
#define C_ITEM_STR_STR(key, str)    {sizeof(key)-1, sizeof(str)-1, (intptr_t)key, (intptr_t)str}
#define C_ITEM_STRKEY_CB(key, cb)   {sizeof(key)-1, 0, (intptr_t)key, (intptr_t)cb}
#define C_ITEM_LIST(key, list)      {0, ARRAY_SIZE(list), (int)key, (intptr_t)list}
#define C_ITEM_STR_LIST(str, list)  {ARRAY_SIZE(str), ARRAY_SIZE(list), (intptr_t)str, (intptr_t)list}

extern c_item_t *       _c_get_item_by_key(c_item_t *list, int list_sz, int key);
extern c_item_t *       _c_get_item_by_strkey(c_item_t *list, int list_sz, const char *key, int key_len);
extern c_item_t *       _c_get_item_by_str(c_item_t *list, int list_sz, const char *str, int str_len);
extern intptr_t         _c_get_data_by_key(c_item_t *list, int list_sz, int key);
extern char *           _c_get_str_by_key(c_item_t *list, int list_sz, int key);
extern char *           _c_get_str_by_strkey(c_item_t *list, int list_sz, const char *key, int key_len);
extern char *           _c_get_strkey_by_str(c_item_t *list, int list_sz, const char *str, int str_len);
extern bool             _c_get_value_by_key(c_item_t *list, int list_sz, int key, uint32_t *dest);
extern bool             _c_get_param_by_key(c_item_t *list, int list_sz, int key, uint32_t *dest);

#define c_get_item_by_key(list, key)        _c_get_item_by_key(ARRAY_AND_SIZE(list), key)
#define c_get_item_by_strkey(list, key)     _c_get_item_by_strkey(ARRAY_AND_SIZE(list), key, strlen(key))
#define c_get_item_by_str(list, str)        _c_get_item_by_str(ARRAY_AND_SIZE(list), str, strlen(str))
#define c_get_data_by_key(list, key)        _c_get_data_by_key(ARRAY_AND_SIZE(list), key)
#define c_get_cb_by_key(list, key)          _c_get_data_by_key(ARRAY_AND_SIZE(list), key)
#define c_get_str_by_key(list, key)         _c_get_str_by_key(ARRAY_AND_SIZE(list), key)
#define c_get_str_by_strkey(list, key)      _c_get_str_by_strkey(ARRAY_AND_SIZE(list), key, strlen(key))
#define c_get_strkey_by_str(list, str)      _c_get_strkey_by_str(ARRAY_AND_SIZE(list), str, strlen(str))
#define c_get_value_by_key(list, key, dst)  _c_get_value_by_key(ARRAY_AND_SIZE(list), key, dst)
#define c_get_param_by_key(list, key, dst)  _c_get_param_by_key(ARRAY_AND_SIZE(list), key, dst)


#endif /* CONST_H_INCLUDED */
