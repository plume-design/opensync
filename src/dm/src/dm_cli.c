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
#include <stdlib.h>
#include <getopt.h>

#include "osp_unit.h"
#include "reboot_flags.h"
#include "target.h"

#include "dm.h"


#define DM_CLI_SHOW_INFO_ID         "id"
#define DM_CLI_SHOW_INFO_SERIAL     "serial_number"
#define DM_CLI_SHOW_INFO_MODEL      "model"
#define DM_CLI_SHOW_INFO_F_VERSION  "firmware_version"
#define DM_CLI_SHOW_INFO_P_VERSION  "platform_version"
#define DM_CLI_SHOW_INFO_SKU_NUMBER     "sku_number"
#define DM_CLI_SHOW_INFO_VENDOR_NAME    "vendor_name"
#define DM_CLI_SHOW_INFO_VENDOR_PART    "vendor_part"
#define DM_CLI_SHOW_INFO_MANUFACTURER   "manufacturer"
#define DM_CLI_SHOW_INFO_FACTORY        "factory"
#define DM_CLI_SHOW_INFO_MFG_DATE       "manufacturer_date"


static int dm_cli_show_info(char *opt)
{
    char buf[128];
    int  buflen = sizeof(buf);

    if (!opt || !strcmp(opt, DM_CLI_SHOW_INFO_ID))
    {
        printf(DM_CLI_SHOW_INFO_ID"=%s\n", osp_unit_id_get(buf, buflen) ? buf : "?");
    }
    if (!opt || !strcmp(opt, DM_CLI_SHOW_INFO_SERIAL))
    {
        printf(DM_CLI_SHOW_INFO_SERIAL"=%s\n", osp_unit_serial_get(buf, buflen) ? buf : "?");
    }
    if (!opt || !strcmp(opt, DM_CLI_SHOW_INFO_MODEL))
    {
        printf(DM_CLI_SHOW_INFO_MODEL"=%s\n", osp_unit_model_get(buf, buflen) ? buf : "?");
    }
    if (!opt || !strcmp(opt, DM_CLI_SHOW_INFO_F_VERSION))
    {
        printf(DM_CLI_SHOW_INFO_F_VERSION"=%s\n", osp_unit_sw_version_get(buf, buflen) ? buf : "?");
    }
    if (!opt || !strcmp(opt, DM_CLI_SHOW_INFO_P_VERSION))
    {
        printf(DM_CLI_SHOW_INFO_P_VERSION"=%s\n", osp_unit_platform_version_get(buf, buflen) ? buf : "?");
    }
    if (!opt || !strcmp(opt, DM_CLI_SHOW_INFO_SKU_NUMBER))
    {
        printf(DM_CLI_SHOW_INFO_SKU_NUMBER"=%s\n", osp_unit_sku_get(buf, buflen) ? buf : "?");
    }
    if (!opt || !strcmp(opt, DM_CLI_SHOW_INFO_VENDOR_NAME))
    {
        printf(DM_CLI_SHOW_INFO_VENDOR_NAME"=%s\n", osp_unit_vendor_name_get(buf, buflen) ? buf : "?");
    }
    if (!opt || !strcmp(opt, DM_CLI_SHOW_INFO_VENDOR_PART))
    {
        printf(DM_CLI_SHOW_INFO_VENDOR_PART"=%s\n", osp_unit_vendor_part_get(buf, buflen) ? buf : "?");
    }
    if (!opt || !strcmp(opt, DM_CLI_SHOW_INFO_MANUFACTURER))
    {
        printf(DM_CLI_SHOW_INFO_MANUFACTURER"=%s\n", osp_unit_manufacturer_get(buf, buflen) ? buf : "?");
    }
    if (!opt || !strcmp(opt, DM_CLI_SHOW_INFO_FACTORY))
    {
        printf(DM_CLI_SHOW_INFO_FACTORY"=%s\n", osp_unit_factory_get(buf, buflen) ? buf : "?");
    }
    if (!opt || !strcmp(opt, DM_CLI_SHOW_INFO_MFG_DATE))
    {
        printf(DM_CLI_SHOW_INFO_MFG_DATE"=%s\n", osp_unit_mfg_date_get(buf, buflen) ? buf : "?");
    }

    return DM_CLI_DONE_OK;
}

static int dm_cli_help()
{
    printf("Usage:\n");
    printf("  dm [options]\n");
    printf("Options:\n");
    printf("  -h, --help          display this help message\n");
    printf("  -v, --verbose       increase verbosity\n");
    printf("  -i, --show-info [field name]\n");
    printf("                      display basic device info\n");
    printf("  -k, --stop-all      kill the managers\n");
    printf("  -k -e, --stop-all --except [manager_1,manager_2,...,manager_n]\n");
    printf("                      list of except killing managers\n");
    printf("                      ex: --stop-all --except cm,nm,fsm\n");
    printf("                      ex: -k -e cm,nm\n");
    printf("  -n -s, --no-reboot --set <module name>\n");
    printf("  -n -c, --no-reboot --clear <module name>\n");
    printf("  -n -g, --no-reboot --get <module name>\n");
    printf("  -n -C, --no-reboot --clear-all\n");
    printf("  -n -L, --no-reboot --list\n");
    return DM_CLI_DONE_OK;
} 

/**
 * dm_cli - handles DM command line arguments
 */
int dm_cli(int argc, char *argv[], log_severity_t *log_severity)
{

    int opt;
    int verbose = 0;
    bool stop_all = false;
    bool except = false;
    char *except_mgr_list = NULL;
    bool no_reboot = false;
    struct option long_options[] =
    {
        { .name = "help",          .has_arg = no_argument,       .val = 'h', },
        { .name = "verbose",       .has_arg = no_argument,       .val = 'v', },
        { .name = "show-info",     .has_arg = optional_argument, .val = 'i', },
        { .name = "stop-all",      .has_arg = optional_argument, .val = 'k', },
        { .name = "except",        .has_arg = optional_argument, .val = 'e', },
        { .name = "no-reboot",     .has_arg = no_argument,       .val = 'n', },
        { .name = "set",           .has_arg = required_argument, .val = 's'  },
        { .name = "clear",         .has_arg = required_argument, .val = 'c'  },
        { .name = "get",           .has_arg = required_argument, .val = 'g'  },
        { .name = "clear-all",     .has_arg = no_argument,       .val = 'C'  },
        { .name = "list",          .has_arg = no_argument,       .val = 'L'  },
        { NULL, 0, 0, 0},
    };

    while ((opt = getopt_long(argc, argv, "hvike::ns:c:g:CL", long_options, NULL)) != -1)
    {
        switch (opt)
        {
            case 'h':
                dm_cli_help();
                return DM_CLI_DONE_OK;
            case 'v':
                /* Handle log verbositiy level. This keeps compatibility with
                 * other managers that use os_opt_get().
                 */
                verbose += 1;
                *log_severity = (verbose == 0) ? LOG_SEVERITY_INFO :
                                (verbose == 1) ? LOG_SEVERITY_DEBUG :
                                                 LOG_SEVERITY_TRACE;
                /* This options allows DM to run as a daemon */
                break;
            case 'i':
                dm_cli_show_info(optind < argc ? argv[optind] : NULL);
                return DM_CLI_DONE_OK;
            case 'k':
                stop_all = true;
                break;
            case 'e' :
                except = true;
                except_mgr_list = argv[optind];
                break;
            case 'n' :
                if (!stop_all && !except)
                {
                    no_reboot = true;
                    target_log_open("DM_CLI_NO_REBOOT", 0);
                    break;
                }
                dm_cli_help();
                return DM_CLI_DONE_ERR;
            case 's' :
                if (no_reboot)
                {
                    if (dm_no_reboot_set(optarg))
                    {
                        return DM_CLI_DONE_OK;
                    }
                    return DM_CLI_DONE_ERR;
                }
                dm_cli_help();
                return DM_CLI_DONE_ERR;
            case 'c' :
                if (no_reboot)
                {
                    if (dm_no_reboot_clear(optarg))
                    {
                        return DM_CLI_DONE_OK;
                    }
                    return DM_CLI_DONE_ERR;
                }
                dm_cli_help();
                return DM_CLI_DONE_ERR;
            case 'g' :
                if (no_reboot)
                {
                    if (dm_no_reboot_get(optarg))
                    {
                        return DM_CLI_DONE_OK;
                    }
                    return DM_CLI_DONE_ERR;
                }
                dm_cli_help();
                return DM_CLI_DONE_ERR;
            case 'C' :
                if (no_reboot)
                {
                    if (dm_no_reboot_clear_all())
                    {
                        return DM_CLI_DONE_OK;
                    }
                    return DM_CLI_DONE_ERR;
                }
                dm_cli_help();
                return DM_CLI_DONE_ERR;
            case 'L' :
                if (no_reboot)
                {
                    if (dm_no_reboot_list())
                    {
                        return DM_CLI_DONE_OK;
                    }
                    return DM_CLI_DONE_ERR;
                }
                dm_cli_help();
                return DM_CLI_DONE_ERR;
            case '?':
            case ':':
            default:
                dm_cli_help();
                return DM_CLI_DONE_ERR;
        }
    }

    if (stop_all)
    {
        if (dm_manager_stop_all(except_mgr_list))
        {
            return DM_CLI_DONE_OK;
        }
        return DM_CLI_DONE_ERR;
    }

    if (except && !stop_all)
    {
        dm_cli_help();
        return DM_CLI_DONE_ERR;
    }

    return DM_CLI_CONTINUE;
}
