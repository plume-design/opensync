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

#include <ctype.h>
#include <unistd.h>

#include "execsh.h"
#include "log.h"
#include "util.h"

#include "osn_ipset.h"

/** Maximum length of an ipset */
#define OSN_IPSET_NAME_LEN      32
/** Maximum of entries to be clustered together */
#define OSN_IPSET_PENDING_MAX   128
/** Temporary file name used for `ipset restore` */
#define OSN_IPSET_RESTORE_FILE  "/tmp/ipset_restore.tmp"

struct osn_ipset
{
    /** ipset name */
    char                ips_name[OSN_IPSET_NAME_LEN];
    enum osn_ipset_type ips_type;
    char               *ips_options;
    /* True if this set has an active temporary set */
    bool                ips_tset;
};

static const char *osn_ipset_type_to_str(enum osn_ipset_type type);
static bool osn_ipset_options_valid(const char *options);
static bool osn_ipset_write_restore_file(const char *name, const char *values[], int values_len, bool add);
static void osn_ipset_tmp_name(char *tmp, size_t tmp_len, const char *name);
static bool osn_ipset_values_modify(osn_ipset_t *self, bool add, const char *values[], int values_len);

static bool osn_ipset_cmd_create(
        const char *name,
        enum osn_ipset_type type,
        const char *options);

static bool osn_ipset_cmd_destroy(const char *name);
static bool osn_ipset_cmd_restore(const char *path);
static bool osn_ipset_cmd_swap(const char *name1, const char *name2);

/*
 * Mapping between IPSET enums and string types
 */
const char *osn_ipset_type_str[] =
{
    [OSN_IPSET_BITMAP_IP] = "bitmap:ip",
    [OSN_IPSET_BITMAPIP_MAC] = "bitmap:ip,mac",
    [OSN_IPSET_BITMAP_PORT] = "bitmap:port",
    [OSN_IPSET_HASH_IP] = "hash:ip",
    [OSN_IPSET_HASH_MAC] = "hash:mac",
    [OSN_IPSET_HASH_IP_MAC] = "hash:ip,mac",
    [OSN_IPSET_HASH_NET] = "hash:net",
    [OSN_IPSET_HASH_NET_NET] = "hash:net,net",
    [OSN_IPSET_HASH_IP_PORT] = "hash:ip,port",
    [OSN_IPSET_HASH_NET_PORT] = "hash:net,port",
    [OSN_IPSET_HASH_IP_PORT_IP] = "hash:ip,port,ip",
    [OSN_IPSET_HASH_IP_PORT_NET] = "hash:ip,port,net",
    [OSN_IPSET_HASH_IP_MARK] = "hash:ip,mark",
    [OSN_IPSET_HASH_NET_PORT_NET] = "hash:net,port,net",
    [OSN_IPSET_HASH_NET_IFACE] = "hash:net,iface",
    [OSN_IPSET_LIST_SET] = "list:set"
};

/*
 * Global initialization function for ipset, called only once
 */
bool osn_ipset_global_init(void)
{
    return osn_ipset_cmd_destroy(NULL);
}

osn_ipset_t* osn_ipset_new(
        const char *name,
        enum osn_ipset_type type,
        const char *options)
{
    osn_ipset_t *self;

    static bool first = true;

    if (first)
    {
        if (!osn_ipset_global_init())
        {
            LOG(DEBUG, "ipset: Global initialization failed.");
            return NULL;
        }
        first = false;
    }

    /* Create a new ipset */
    if (!osn_ipset_cmd_create(name, type, options))
    {
        LOG(ERR, "ipset: %s: Ipset create failed.", name);
        return NULL;
    }

    self = calloc(1, sizeof(*self));
    STRSCPY(self->ips_name, name);
    self->ips_type = type;
    self->ips_options = strdup(options);

    return self;
}

/**
 * IPSET deinitialization function.
 */
void osn_ipset_del(osn_ipset_t *self)
{
    if (!osn_ipset_cmd_destroy(self->ips_name))
    {
        LOG(ERR, "ipset: %s: Error destroying ipset.", self->ips_name);
    }

    free(self->ips_options);
    free(self);
}

/**
 * Apply
 */
bool osn_ipset_apply(osn_ipset_t *self)
{
    char tset[OSN_IPSET_NAME_LEN];
    (void)self;

    /* No-op if there's no temporary set */
    if (!self->ips_tset) return true;

    /*
     * If there's a temporary set, swap the temporary and the current set and
     * destroy the temporary one
     */
    osn_ipset_tmp_name(tset, sizeof(tset), self->ips_name);

    if (!osn_ipset_cmd_swap(tset, self->ips_name))
    {
        LOG(ERR, "ipset: %s: Error swapping temporary restore set %s.", self->ips_name, tset);
        return false;
    }

    (void)osn_ipset_cmd_destroy(tset);

    self->ips_tset = false;

    return true;
}

/**
 * Use `ipset swap` to guarantee some atomicity when replacing the values in the set
 */
bool osn_ipset_values_set(osn_ipset_t *self, const char *values[], int values_len)
{
    char tset[OSN_IPSET_NAME_LEN];

    bool retval = false;

    osn_ipset_tmp_name(tset, sizeof(tset), self->ips_name);

    /*
     * Create the temporary ipset -- must use the same type and options as the
     * original.
     */

    /* Create the temporary set */
    if (!self->ips_tset)
    {
        if (!osn_ipset_cmd_create(tset, self->ips_type, self->ips_options))
        {
            LOG(ERR, "ipset: %s: Error creating temporary restore ipset.", self->ips_name);
            goto error;
        }
    }

    self->ips_tset = true;

    if (!osn_ipset_write_restore_file(tset, values, values_len, true))
    {
        LOG(ERR, "ipset: %s: Error writing restore file.", self->ips_name);
        goto error;
    }

    /* Execute commands from the restore file */
    if (!osn_ipset_cmd_restore(OSN_IPSET_RESTORE_FILE))
    {
        LOG(ERR, "ipset: %s: Error restoring temporary set.", self->ips_name);
        goto error;
    }

    retval = true;

error:

    if (unlink(OSN_IPSET_RESTORE_FILE) != 0)
    {
        LOG(WARN, "ipset: %s: Error removing temporary restore file during set: %s",
                self->ips_name, OSN_IPSET_RESTORE_FILE);
    }

    return retval;
}


/**
 * Add values from set
 */
bool osn_ipset_values_add(osn_ipset_t *self, const char *values[], int values_len)
{
    if (!osn_ipset_values_modify(self, true, values, values_len))
    {
        LOG(ERR, "ipset: %s: Error adding values.", self->ips_name);
        return false;
    }

    return true;
}


/**
 * Removes values from set
 */
bool osn_ipset_values_del(osn_ipset_t *self, const char *values[], int values_len)
{
    if (!osn_ipset_values_modify(self, false, values, values_len))
    {
        LOG(ERR, "ipset: %s: Error deleting values.", self->ips_name);
        return false;
    }

    return true;
}

/*
 * ===========================================================================
 *  Utility/static functions
 * ===========================================================================
 */
const char* osn_ipset_type_to_str(enum osn_ipset_type type)
{
    if (type >= ARRAY_LEN(osn_ipset_type_str))
    {
        return NULL;
    }

    return osn_ipset_type_str[type];
}

bool osn_ipset_options_valid(const char *options)
{
    const char *popt = options;

    static const char accept[] = " /,.:-";

    for (popt = options; *popt != '\0'; popt++)
    {
        if (isalnum(*popt)) continue;
        if (strchr(accept, *popt) != NULL) continue;

        return false;
    }

    return true;
}

bool osn_ipset_write_restore_file(const char *name, const char *values[], int values_len, bool add)
{
    FILE *ir;
    int ii;

    ir = fopen(OSN_IPSET_RESTORE_FILE, "w+");
    if (ir == NULL)
    {
        LOG(DEBUG, "ipset: %s: Error creating restore file: %s",
                name, OSN_IPSET_RESTORE_FILE);
        return false;
    }

    for (ii = 0; ii < values_len; ii++)
    {
        fprintf(ir, "-exist %s %s %s\n",
                add ? "add" : "del", name, values[ii]);
    }

    fclose(ir);

    return true;
}

void osn_ipset_tmp_name(char *tmp, size_t tmp_len, const char *name)
{
    size_t mark_pos = strlen(name);

    strscpy(tmp, name, tmp_len);

    if (mark_pos >= tmp_len)
    {
        mark_pos = tmp_len - 1;
    }

    tmp[mark_pos++] = '?';
    tmp[mark_pos++] = '\0';
}

bool osn_ipset_values_modify(osn_ipset_t *self, bool add, const char *values[], int values_len)
{
    char tset[OSN_IPSET_NAME_LEN];

    bool retval = false;

    /*
     * If we have a temporary ipset active, add the entries to it. Otherwise
     * just append values to the current set. It's OK if the operation is not
     * atomic.
     */
    if (self->ips_tset)
    {
        osn_ipset_tmp_name(tset, sizeof(tset), self->ips_name);
    }
    else
    {
        STRSCPY(tset, self->ips_name);
    }

    if (!osn_ipset_write_restore_file(tset, values, values_len, add))
    {
        LOG(DEBUG, "ipset: %s: Error writing restore file.", self->ips_name);
        goto error;
    }

    if (!osn_ipset_cmd_restore(OSN_IPSET_RESTORE_FILE))
    {
        LOG(DEBUG, "ipset: %s: Error removing/adding[%d] values to set.",
                self->ips_name, add);
        goto error;
    }

    retval = true;

error:
    if (unlink(OSN_IPSET_RESTORE_FILE) != 0)
    {
        LOG(WARN, "ipset: %s: Error removing temporary restore file during remove/add[%d]: %s",
                self->ips_name, add, OSN_IPSET_RESTORE_FILE);
    }

    return retval;
}

bool osn_ipset_cmd_create(
        const char *name,
        enum osn_ipset_type type,
        const char *options)
{
    const char *stype;
    int rc;

    if (!osn_ipset_options_valid(options))
    {
        LOG(DEBUG, "ipset: Invalid create options: %s", options);
        return false;
    }

    stype = osn_ipset_type_to_str(type);
    if (stype == NULL)
    {
        LOG(DEBUG, "ipset: Invalid ipset type: %d", type);
        return false;
    }

    /*
     * If the set doesn't exist, ipset destroy will fail; ignore errors.
     */
    (void)execsh_log(
            LOG_SEVERITY_DEBUG,
            _S(ipset -quiet destroy "$1"),
            (char *)name);

    rc = execsh_log(
            LOG_SEVERITY_DEBUG,
            _S(ipset -exist create "$1" "$2" $3),
            (char *)name,
            (char *)stype,
            options == NULL ? "" : (char *)options);
    if (rc != 0)
    {
        LOG(DEBUG, "ipset: %s: ipset create command failed.", name);
        return false;
    }

    return true;
}

bool osn_ipset_cmd_destroy(const char *name)
{
    int rc;

    if (name != NULL)
    {
        rc = execsh_log(LOG_SEVERITY_DEBUG, _S(ipset destroy "$1"), (char *)name);
    }
    else
    {
        rc = execsh_log(LOG_SEVERITY_DEBUG, _S(ipset destroy));
    }

    return (rc == 0);
}

bool osn_ipset_cmd_restore(const char *path)
{
    int rc;
    rc = execsh_log(LOG_SEVERITY_DEBUG, _S(ipset restore -file "$1"), (char *)path);
    return (rc == 0);
}

bool osn_ipset_cmd_swap(const char *name1, const char *name2)
{
    int rc;
    rc = execsh_log(LOG_SEVERITY_DEBUG, _S(ipset swap "$1" "$2"), (char *)name1, (char *)name2);
    return (rc == 0);
}
