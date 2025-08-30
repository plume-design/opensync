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
#include <stddef.h>
#include <netinet/in.h>
#include <assert.h>

/*
 * ===========================================================================
 *  String constants
 *  _LEN - refers to string length, the length must include the terminating \0 character
 *  _SZ  - refers to sizes of stuff, as returned by sizeof()
 *  _MAX - refers to a maximum value of a numeric value, or maximum array length (for example, maximum number of interfaces)
 *  _MIN - refers to a minimum value of a numeric value
 * ===========================================================================
 */
#define C_HOSTNAME_LEN  64                              /**< Maximum hostname name (without FQDN) */
#define C_FQDN_LEN      256                             /**< Fully qualified hostname maximum length */
#define C_IFNAME_LEN    65                              /**< Interface name size, including trailing \0 */
#define C_MACADDR_LEN   sizeof("11:22:33:44:55:66")     /**< Maximum length of a MAC address represented as string */
#define C_IP4ADDR_LEN   INET_ADDRSTRLEN                 /**< Maximum length of an IP address represented as string */
#define C_IPV6ADDR_LEN  \
        (INET6_ADDRSTRLEN + sizeof("/32") - 1)          /**< Maximum length of an IPv6 address including suffix */
#define C_MAXPATH_LEN   256                             /**< Shorter than MAXPATH, but sufficient to access any path on the device */
#define C_WPA_PSK_LEN   64                              /**< WPA* PSK maximum length */
#define C_INT8_LEN      sizeof("-255")                  /**< Space needed to represent a  8-bit integer */
#define C_INT16_LEN     sizeof("-65536")                /**< Space needed to represent a 16-bit integer */
#define C_INT32_LEN     sizeof("-4294967296")           /**< Space needed to represent a 32-bit integer */
#define C_INT64_LEN     sizeof("-18446744073709551616") /**< Space needed to represent a 64-bit integer */
#define C_HEXINT8_LEN   sizeof("-0xFF")                 /**< Space needed to represent a  8-bit hex integer */
#define C_HEXINT16_LEN  sizeof("-0xFFFF")               /**< Space needed to represent a 16-bit hex integer */
#define C_HEXINT32_LEN  sizeof("-0xFFFFFFFF")           /**< Space needed to represent a 32-bit hex integer */
#define C_HEXINT64_LEN  sizeof("-0xFFFFFFFFFFFFFFFF")   /**< Space needed to represent a 64-bit hex integer */
#define C_PID_LEN       C_INT64_LEN
#define C_QOS_MAP_LEN \
        sizeof("0:0 1:0 2:0 3:0 4:0 5:0 6:0 7:0")       /**< Maximum length of VLAN QOS map string */
#define C_USERNAME_LEN  256                             /**< Length of usernames */
#define C_PASSWORD_LEN  256                             /**< Length of passwords */

/*
 * ===========================================================================
 *  Musl does not define __GNUC_PREREQ
 * ===========================================================================
 */

#ifndef __GNUC_PREREQ
#if defined(__GNUC__) && defined(__GNUC_MINOR__)
#define	__GNUC_PREREQ(__maj, __min)	\
	(__GNUC__ > (__maj) || __GNUC__ == (__maj) && __GNUC_MINOR__ >= (__min))
#else
#define	__GNUC_PREREQ(__maj, __min) 0
#endif
#endif

/*
 * ===========================================================================
 *  Various system constants
 * ===========================================================================
 */
#define C_INVALID_PID   -1

/*
 * ===========================================================================
 *  Networking constants
 * ===========================================================================
 */
#define C_VLAN_MIN      0                               /**< Minimum VLAN ID */
#define C_VLAN_MAX      4094                            /**< Maximum VLAN ID */
#define C_VLAN_INVALID  -1                              /**< Invalid VLAN */
#define C_VLAN_LEN      sizeof("4094")                  /**< Maximum size of VLAN represented as string */

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
extern c_item_t *       _c_get_item_by_strkey(c_item_t *list, int list_sz, const char *key);
extern c_item_t *       _c_get_item_by_str(c_item_t *list, int list_sz, const char *str);
extern intptr_t         _c_get_data_by_key(c_item_t *list, int list_sz, int key);
extern char *           _c_get_str_by_key(c_item_t *list, int list_sz, int key);
extern char *           _c_get_str_by_strkey(c_item_t *list, int list_sz, const char *key);
extern char *           _c_get_strkey_by_str(c_item_t *list, int list_sz, const char *str);
extern bool             _c_get_value_by_key(c_item_t *list, int list_sz, int key, uint32_t *dest);
extern bool             _c_get_param_by_key(c_item_t *list, int list_sz, int key, uint32_t *dest);

#define c_get_item_by_key(list, key)        _c_get_item_by_key(ARRAY_AND_SIZE(list), key)
#define c_get_item_by_strkey(list, key)     _c_get_item_by_strkey(ARRAY_AND_SIZE(list), key)
#define c_get_item_by_str(list, str)        _c_get_item_by_str(ARRAY_AND_SIZE(list), str)
#define c_get_data_by_key(list, key)        _c_get_data_by_key(ARRAY_AND_SIZE(list), key)
#define c_get_cb_by_key(list, key)          _c_get_data_by_key(ARRAY_AND_SIZE(list), key)
#define c_get_str_by_key(list, key)         _c_get_str_by_key(ARRAY_AND_SIZE(list), key)
#define c_get_str_by_strkey(list, key)      _c_get_str_by_strkey(ARRAY_AND_SIZE(list), key)
#define c_get_strkey_by_str(list, str)      _c_get_strkey_by_str(ARRAY_AND_SIZE(list), str)
#define c_get_value_by_key(list, key, dst)  _c_get_value_by_key(ARRAY_AND_SIZE(list), key, dst)
#define c_get_param_by_key(list, key, dst)  _c_get_param_by_key(ARRAY_AND_SIZE(list), key, dst)

/*
 * ===========================================================================
 *  SET operations
 *
 *  Utility functions for building sets out of enums
 * ===========================================================================
 */

/* Calculate the array size needed for the set using the type @p type */
#define C_SET_LEN(desc, type)        ((desc + (sizeof(type) * 8 - 1)) / (sizeof(type) * 8))

#define __C_ISZ(x)                  (sizeof(x[0]) * 8)

#define __C_SET_A(cset, bit)        (((uint8_t *)cset)[(bit) >> 3 ])
#define __C_SET_BIT( bit)           (1 << ((bit) & 7))

/* Returns true (bool) if bit is set */
#define C_IS_SET(cset, bit)         ((bool)(__C_SET_A(cset, bit) & __C_SET_BIT(bit)))
/* Set bit */
#define C_SET_ADD(cset, bit)        (__C_SET_A(cset, bit) |= __C_SET_BIT(bit))
/* Clear bit */
#define C_SET_DEL(cset, bit)        (__C_SET_A(cset, bit) &= ~__C_SET_BIT(bit))
/* Toggle bit */
#define C_SET_TOG(cset, bit)        (__C_SET_A(cset, bit) ^= __C_SET_BIT(bit))
/* Set bit to value 1 - set, 0 - clear */
#define C_SET_VAL(cset, bit, val)   (__C_SET_A(cset, bit) ^= ((val) == C_IS_SET(cset, bit)) ? 0 : __C_SET_BIT(bit))

/*
 * Example code
 *
 * enum my_set
 * {
 *      MY_OPTION_1,
 *      MY_OPTION_2,
 *      MY_OPTION_3 = 13,
 *      MY_OPTION_MAX,
 * };
 *
 * uint8_t  my_option_bytes[C_SET_LEN(MY_OPTION_MAX, uint8_t)]; // ARRAY_LEN(my_option_bytes) == 2
 * uint32_t my_option_ints[C_SET_LEN(MY_OPTION_MAX, uint32_t)]; // ARRAY_LEN(my_option_ints) == 1
 *
 * CSET_ADD(my_option_bytes, MY_OPTION_3);
 *
 * CSET_IS(my_option_bytes, MY_OPTION_3) == true
 * CSET_IS(my_option_bytes, MY_OPTION_1) == false
 */

/**
 * PRI(type) produces a constant string that is suitable for printf(), for example:
 *
 * Example:
 *      printf("MAC address is: " PRI(os_macaddr_t), FMT(os_macaddr_t, my_mac));
 */
#define PRI(type)               PRI_ ## type
/**
 * FMT(type, value) formats the value in such a way that is suitable for input
 * to printf with a PRI(type) format string
 *
 * Example:
 *      printf("IP address is: " PRI(os_ipaddr_t), FMT(os_ipaddr_t, my_ip));
 */
#define FMT(type, x)            FMT_ ## type (x)

#define PRI_int64_t             PRId64
#define PRI_uint64_t            PRIu64
#define PRI_intmax_t            "%jd"

/**
 * Pack a variable argument list into a char *[] array. If you find the syntax
 * below odd, google up C99 compound literals.
 *
 * This is useful for doing functions that behave like printf without the need
 * of a format identifier.
 *
 * The __V() macro is there just to get rid of the leading comma in case
 * C_VPACK() was called without arguments.
 *
 * #define print_strings(...) __print_strings(C_VPACK(__VA_ARGS__))
 *
 * __print_strings(char *argv[])
 * {
 *     char *parg;
 *
 *     for (parg = argv; *parg != NULL) parg++) { printf("%s\n", *parg); }
 * }
 *
 * print_strrings("hello", "world")   -> __print_strings{ "hello", "world", NULL });
 *
 */
#define __V(x, ...)                     __VA_ARGS__
#define C_VPACK(...)                    ((char *[]){ __V(dummy, ##__VA_ARGS__, NULL) })
#define C_CVPACK(...)                   ((const char *[]){ __V(dummy, ##__VA_ARGS__, NULL) })

/**
 * Same as VPACK, except it can take any array type and custom array terminator value
 *
 * VPACK is essentially the same as C_VPACK(char *[], NULL, ##__VA_ARGS__ )
 *
 * ======================================================================================
 * NOTE: Make sure to use ##__VA_ARGS__ as argument to C_XPACK() instead of __VA_ARGS__
 * so the case with 0 arguments is handled properly. For example:
 * 
 * __print_ints(int *nums[])
 * {
 *      int *p;
 *
 *      for (p = nums; *p >= 0; *p++)
 *      {
 *           printf("%d\n");
 *      }
 * }
 *
 * #define print_ints1(C_XPACK(int *[], -1, __VA_ARGS__))
 * #define print_ints2(C_XPACK(int *[], -1, ##__VA_ARGS__))
 *
 * print_ints1() <- FAIL
 * print_ints2() <- OK
 *
 * ======================================================================================
 */
#define C_XPACK(type, term, ...)        ((type){ __V(dummy, ##__VA_ARGS__, (term)) })

/**
 * Static assertions: The macro below causes a compile time error if COND is false.
 */

#if (defined(__GNUC__) && __GNUC_PREREQ(4,7)) || defined(__clang__)

#define C_STATIC_ASSERT(COND,MSG) _Static_assert((COND), MSG)

#else

/*
 * XXX: The code below produces a warning with -Wall, make sure to add -Wno-unused-local-typedefs when using
 * this version.
 *
 * The _LINE macros are there just to append __LINE__ to the symbol name.
 */
#define __LINE2(A, L)   A##L
#define __LINE1(A, L)   __LINE2(A, L)

#define C_STATIC_ASSERT(COND,MSG) typedef char __LINE1(___STATIC_ASSERT,__LINE__)[(COND)?1:-1]

#endif

/*
 * Return the size of a structure member
 */
#define C_FIELD_SZ(type, field)     sizeof(((type *)NULL)->field)


/*
 * These are used to help working with bit-packed values.
 * Bit-packed values are often found in protocol headers, hw
 * register definitions, etc.
 *
 * #define REG_CHAN C_MASK_GEN(7, 2) // =0xfc
 * #define REG_WIDTH 0x03
 *
 * int reg = C_MASK_PREP(REG_CHAN, 4)
 *         | C_MASK_PREP(REG_WIDTH, 1);
 *
 * int chan = C_MASK_GET(REG_CHAN, reg); // =4
 * int width = C_MASK_GET(REG_WIDTH, reg); // =1
 */
#define __bf_shf(x) (__builtin_ffsll(x) - 1)
#define C_BIT(x) (1ULL << (x))
#define C_MASK_GEN(high_, low_) (((1 << (high_)) - 1) + (1 << (high_)) - ((1 << (low_)) - 1))
#define C_MASK_PREP(mask_, value_) (((typeof(mask_))(value_) << __bf_shf(mask_)) & (mask_))
#define C_MASK_GET(mask_, value) ((typeof(mask_))(((value) & (mask_)) >> __bf_shf(mask_)))

/*
 * Bound checking helpers for nested, and variable-length
 * encoded structures.
 *
 * Example usage:
 *
 *  struct foo {
 *    uint32_t bar;
 *    uint32_t baz;
 *  };
 *
 *  char buf1[10] = {0};
 *  size_t len1 = 10;
 *
 *  char buf2[5] = {0};
 *  size_t len2 = 5;
 *
 *  size_t rem;
 *  const uint32_t *a = C_FIELD_GET_REM(buf1, len1, rem, struct foo, bar);
 *  assert(a != NULL);
 *  assert(rem == 6);
 *
 *  const uint32_t *b = C_FIELD_GET_REM(buf1, len1, rem, struct foo, baz);
 *  assert(b != NULL);
 *  assert(rem == 2);
 *
 *  const uint32_t *c = C_FIELD_GET_REM(buf2, len2, rem, struct foo, bar);
 *  assert(c != NULL);
 *  assert(rem == 1);
 *
 *  const uint32_t *d = C_FIELD_GET_REM(buf2, len2, rem, struct foo, baz);
 *  assert(d == NULL);
 *  // rem undefined
 *
 */
#define C_FIELD_END(type, field)           (offsetof(type, field) + C_FIELD_SZ(type, field))
#define C_FIELD_GET(buf, len, type, field) (((len) >= C_FIELD_END(type, field)) \
                                            ? ((const void *)(buf) + offsetof(type, field)) \
                                            : NULL)

#define C_FIELD_GET_REM(buf, len, rem, type, field) ({ \
        const typeof (((const type *)NULL)->field) *buf_safe \
            = (const void *)C_FIELD_GET(buf, len, type, field); \
        rem = (len) - C_FIELD_END(type, field); \
        buf_safe; \
    })

/*
 * =============================================================================
 * c_fmt(), c_fmt_arg() is used to annotate functions that accept a printf-like
 * syntax. Used for enriched compiler warnings.
 */
#if defined(__GNUC__)
/* `fmt` specifies the argument number that accepts a printf-like format string */
#define c_fmt_arg(fmt) __attribute__((format_arg(fmt)))
/*
 * `fmt` specifies the argument number that accepts a printf-like format, arg is
 * the first argument of the variable argument list
 */
#define c_fmt(fmt, arg) __attribute__((format(printf, fmt, arg)))

#else
#define c_fmt_arg(fmt)
#define c_fmt(fmt, arg)
#endif /* defined(__GNUC__) */

/*
 * =============================================================================
 * c_auto() and c_auto_ptr() are used to define auto-cleanup types. For example:
 *
 * c_auto(int) socket;
 * c_auto_ptr(FILE) f;
 *
 * In the above example, int_cleanup() and FILE_ptr_cleanup() will be called when
 * the variables go out of scope.
 *
 * c_weak is used for defining weak symbols.
 * =============================================================================
 */
#if defined(__GNUC__)
#define c_auto(x) x __attribute__((cleanup(x##_cleanup), unused))
#define c_auto_ptr(x) *x __attribute__((cleanup(x##_ptr_##_cleanup), unused))
#define c_weak __attribute__((weak))
#else
#warn GCC attributes not supported by compiler, c_auto(), c_auto_ptr(), c_weak will not be available.
#endif

/*
 * =============================================================================
 * Thread local definitions
 * =============================================================================
 */
#if __STDC_VERSION__ >= 201112L /* C11 and above */
#define c_thread_local _Thread_local
#elif defined(__GNUC__)
#define c_thread_local __thread
#else
#error Unsupported compiler, c_thread_local will not be available.
#endif

/*
 * =============================================================================
 * max_align_t was introduced in C11, defined it for compilers <C11
 * =============================================================================
 */
#if __STDC_VERSION__ < 201112L /* below C11 */
union __max_align_t
{
        void *__ptr;
        long double __ld;
        long long __ll;
};
typedef union __max_align_t max_align_t;
#endif

/*
 * =============================================================================
 * Concatenation macro, useful for append line __LINE__
 * =============================================================================
 */
#define __C_CONCAT(x, y) x ## y
#define C_CONCAT(x, y) __C_CONCAT(x, y)

#endif /* CONST_H_INCLUDED */
