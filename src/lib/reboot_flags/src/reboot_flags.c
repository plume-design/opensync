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
 * ===========================================================================
 *  reboot_flags: API to request a critical task not to be disrupted by reboot
 * ===========================================================================
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>

#include "const.h"
#include "os.h"
#include "log.h"

#define CMD_LEN (C_MAXPATH_LEN * 2 + 128)

bool no_reboot_set(char *module_name)
{
    char module_filename[C_MAXPATH_LEN] = {'\0'};
    snprintf(module_filename, sizeof(module_filename), CONFIG_NO_REBOOT_DIR "/%s", module_name);
    int fd = open(module_filename, O_WRONLY | O_CREAT, 0444);
    if (fd <= 0)
    {
        LOGE("NO-REBOOT: no_reboot_set: Error creating file %s", module_filename);
        return false;
    }
    close(fd);
    LOGI("NO-REBOOT: no_reboot_set: Created file %s", module_filename);
    return true;
}

bool no_reboot_clear(char *module_name)
{
    struct stat st;
    if (stat(CONFIG_NO_REBOOT_DIR, &st) || !S_ISDIR(st.st_mode))
    {
        LOGE("NO-REBOOT: no_reboot_clear: No working directory exists.");
        return false;
    }
    char module_filename[C_MAXPATH_LEN] = {'\0'};
    snprintf(module_filename, sizeof(module_filename), CONFIG_NO_REBOOT_DIR "/%s", module_name);
    if (unlink(module_filename) != 0)
    {
        LOGE("NO-REBOOT: no_reboot_clear: Can not delete file %s", module_filename);
        return false;
    }
    LOGI("NO-REBOOT: no_reboot_clear: File deleted: %s", module_filename);
    return true;
}

bool no_reboot_get(char *module_name)
{
    struct stat st;
    if (stat(CONFIG_NO_REBOOT_DIR, &st) || !S_ISDIR(st.st_mode))
    {
        LOGE("NO-REBOOT: no_reboot_get: No working directory exists.");
        return false;
    }
    char module_filename[C_MAXPATH_LEN] = {'\0'};
    snprintf(module_filename, sizeof(module_filename), CONFIG_NO_REBOOT_DIR "/%s", module_name);
    if (access(module_filename, F_OK) == 0)
    {
        LOGI("NO-REBOOT: no_reboot_get returns true as file %s exists", module_filename);
        return true;
    }
    LOGI("NO-REBOOT: no_reboot_get returns false as file %s does not exist", module_filename);
    return false;
}

bool no_reboot_clear_all(void)
{
    DIR *dir;
    struct dirent *entry;
    dir = opendir(CONFIG_NO_REBOOT_DIR);
    if (dir == NULL)
    {
        LOGE("NO-REBOOT: no_reboot_clear_all: Could not open " CONFIG_NO_REBOOT_DIR);
        return false;
    }
    bool unlink_failed_at_least_once = false;
    char module_filename[C_MAXPATH_LEN] = {'\0'};
    while ((entry = readdir(dir)) != NULL)
    {
        if (entry->d_type == DT_REG)
        {
            LOGT("NO-REBOOT: no_reboot_clear_all: Filename: %s", entry->d_name);
            memset(module_filename, '\0', C_MAXPATH_LEN);
            snprintf(module_filename, sizeof(module_filename), CONFIG_NO_REBOOT_DIR "/%s", entry->d_name);
            if (unlink(module_filename) != 0)
            {
                unlink_failed_at_least_once = true;
                LOGE("NO-REBOOT: no_reboot_clear_all: Error deleting file %s", module_filename);
            }
            LOGD("NO-REBOOT: no_reboot_clear_all: File deleted: %s", entry->d_name);
        }
    }
    closedir(dir);
    if (unlink_failed_at_least_once)
    {
        LOGE("NO-REBOOT: no_reboot_clear_all: Some files remained undeleted");
        return false;
    }
    LOGI("NO-REBOOT: no_reboot_clear_all: All files deleted");
    return true;
}

bool no_reboot_dump_modules()
{
    DIR *dir;
    struct dirent *entry;
    dir = opendir(CONFIG_NO_REBOOT_DIR);
    if (dir == NULL)
    {
        LOGE("NO-REBOOT: no_reboot_dump_modules: Could not open " CONFIG_NO_REBOOT_DIR);
        return false;
    }
    while ((entry = readdir(dir)) != NULL)
    {
        if (entry->d_type == DT_REG) printf("%s\n", entry->d_name);
    }
    closedir(dir);
    LOGI("NO-REBOOT: no_reboot_dump_modules: Done");
    return true;
}
