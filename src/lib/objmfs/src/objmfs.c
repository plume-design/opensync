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

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include "const.h"
#include "schema.h"
#include "log.h"
#include "os.h"

#define FIELD_ARRAY_LEN(TYPE,FIELD) ARRAY_LEN(((TYPE*)0)->FIELD)
#define CMD_LEN 1024

/******************************************************************************
 *  Support functions
 *****************************************************************************/

// Remove folder
static bool objmfs_rmdir(char *path)
{
    char cmd[CMD_LEN];
    if (strcmp(path, "/") == 0){
        LOG(ERR, "objmfs: removing / is not allowed: '%s'", path);
        return false;
    }
    sprintf(cmd, "rm -fr %s", path);
    LOG(TRACE, "objmfs: rmdir: %s", cmd);
    if (cmd_log(cmd))
    {
        return false;
    }
    return true;
}

// Create folder and subfolders
static bool objmfs_mkdir(char *path)
{
    char cmd[CMD_LEN];
    sprintf(cmd, "mkdir -p %s", path);
    LOG(TRACE, "objmfs: mkdir: %s", cmd);
    if (cmd_log(cmd))
    {
        return false;
    }
    return true;
}

/******************************************************************************
 *  Public API
 *****************************************************************************/

bool objmfs_install(char *path, char *name, char *version)
{
    FILE *fd;
    char cmd[CMD_LEN];
    char folder_path[PATH_MAX];
    char version_path[PATH_MAX];
    char *line = NULL;

    char object_name[FIELD_ARRAY_LEN(struct schema_Object_Store_Config, name)];
    char object_version[FIELD_ARRAY_LEN(struct schema_Object_Store_Config, version)];
    size_t len = 0;
    ssize_t read;
    bool ret;

    ret = true;
    LOG(DEBUG, "objmfs: (%s): Installing: %s:%s:%s", __func__, name, version, path);

    objmfs_mkdir(CONFIG_OBJMFS_DIR);
    sprintf(folder_path, "%s/%s", CONFIG_OBJMFS_DIR, name);
    objmfs_mkdir(folder_path);
    sprintf(folder_path, "%s/%s/%s", CONFIG_OBJMFS_DIR, name, version);
    objmfs_mkdir(folder_path);

    sprintf(version_path, "%s/version", folder_path);

    objmfs_mkdir(folder_path);

    // Validate md5 of downloaded data
    // Note: currently only tar.gz is supported, .deb package would require additional handling
    sprintf(cmd, "gzip -t %s", path);
    if (cmd_log(cmd))
    {
        LOG(ERR, "objmfs: Integrity check of package failed: %s", cmd);
        ret = false;
        goto cleanup;
    }

    // Extract tarball (or deb package)
    sprintf(cmd, "tar -xzf %s -C %s", path, folder_path);
    LOG(TRACE, "objmfs: Extract base tarball command: %s", cmd);
    if (cmd_log(cmd))
    {
        LOG(ERR, "objmfs: Extraction of package failed: %s", cmd);
        ret = false;
        goto cleanup;
    }

    // Read name & version file and compare with data from ovsdb
    fd = fopen(version_path, "r");
    while ((read = getline(&line, &len, fd)) != -1)
    {
        if (strstr(line, "name") != NULL)
        {
            sscanf(line, "name:%s", object_name);
        }
        if (strstr(line, "version") != NULL)
        {
            sscanf(line, "version:%s", object_version);
        }
    }
    fclose(fd);

    // Validate metadata of object (name, version)
    if (strcmp(object_name, name) != 0)
    {
        LOG(ERR, "objmfs: name mismatch; ovsdb name: '%s'; package_name: '%s'", name, object_name);
        ret = false;
        goto cleanup;
    }

    if (strcmp(object_version, version) != 0)
    {
        LOG(ERR, "objmfs: version mismatch; ovsdb version: '%s'; package version: '%s'", version, object_version);
        ret = false;
        goto cleanup;
    }

    // Extract data directly to storage
    sprintf(cmd, "tar -xzf %s/data.tar.gz -C %s", folder_path, folder_path);
    LOG(TRACE, "objmfs: Extract data command: %s", cmd);
    if (cmd_log(cmd))
    {
        LOG(ERR, "objmfs: Extraction of data failed: %s", cmd);
        ret = false;
        goto cleanup;
    }

cleanup:
    sprintf(folder_path, "%s/data.tar.gz", folder_path);
    objmfs_rmdir(folder_path);
    objmfs_rmdir(path); //clean downloaded tarball

    return ret;
}

bool objmfs_remove(char *name, char *version)
{
    char buf[PATH_MAX];
    LOG(DEBUG, "objmfs: Removing %s version %s from objmfs", name, version);
    sprintf(buf, "%s/%s/%s", CONFIG_OBJMFS_DIR, name, version);
    objmfs_rmdir(buf);
    return true;
}

bool objmfs_path(char *buf, size_t buffsz, char *name, char *version)
{
    int rc;
    snprintf(buf, buffsz, "%s/%s/%s", CONFIG_OBJMFS_DIR, name, version);

    rc = access(buf, F_OK);
    if (rc)
    {
        LOG(ERR, "objmfs: Object does not exist: %s %s: %s", name, version, strerror(errno));
        buf[0] = '\0';
        return false;
    }

    return true;
}
