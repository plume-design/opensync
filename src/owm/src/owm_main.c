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

/* libc */
#include <stdio.h>
#include <unistd.h>

/* opensync */
#include <log.h>
#include <osw_ut.h>
#include <memutil.h>

/* onewifi */
#include <ow_core.h>
#include <osw_etc.h>
#include <kv_parser.h>

enum owm_cli_arg
{
    OWM_CLI_NONE = 0,
    OWM_CLI_FAILURE,
    OWM_CLI_GET,
    OWM_CLI_EXPORT_SH
};

struct owm_main {
    bool ut_list_tests;
    bool ut_verbose;
    bool ut_all;
    bool dont_fork;
    const char *ut_prefix;
    enum owm_cli_arg cli_arg;
    char *cli_key;
};

static struct owm_main g_owm_main = { 0, };

static void
print_usage(char **argv)
{
    fprintf(stderr, "Usage: %s [-h][-l][-t][-T][-u prefix][-U prefix] <args>\n\n", argv[0]);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -h          display this help message\n");
    fprintf(stderr, "  -t          run all unit tests (silent)\n");
    fprintf(stderr, "  -T          run all unit tests (verbose)\n");
    fprintf(stderr, "  -u prefix   run unit tests starting with 'prefix' (silent)\n");
    fprintf(stderr, "  -U prefix   rrun unit tests starting with 'prefix' (verbose)\n");
    fprintf(stderr, "  -g          don't fork (simplify debugging with gdb) \n");
    fprintf(stderr, "  -l          list unit tests\n");
    fprintf(stderr, "Args:\n");
    fprintf(stderr, "  get         get configuration value\n");
    fprintf(stderr, "  export sh   export shell script with environment\n");
}

static void
owm_parse_non_option_arg(struct owm_main *m, int argc, char **argv)
{
    if (optind >= argc)
    {
        m->cli_arg = OWM_CLI_NONE;
        return;
    }
    if (strcmp(argv[optind], "get") == 0)
    {
        if (optind + 1 >= argc) goto error;
        m->cli_arg = OWM_CLI_GET;
        m->cli_key = argv[optind + 1];
    }
    else if (strcmp(argv[optind], "export") == 0)
    {
        if (optind + 1 >= argc) goto error;
        if (strcmp(argv[optind + 1], "sh") == 0) m->cli_arg = OWM_CLI_EXPORT_SH;
        else goto error;
    }
    else
    {
        goto error;
    }
    return;
error:
    m->cli_arg = OWM_CLI_FAILURE;
}

static void
owm_main_parse_args(struct owm_main *m, int argc, char **argv)
{
     int opt = 0;

     while ((opt = getopt(argc, argv, "hWtTu:U:gl")) != -1) {
        switch(opt) {
            case 't':
                m->ut_verbose = false;
                m->ut_all = true;
                break;
            case 'T':
                m->ut_verbose = true;
                m->ut_all = true;
                break;
            case 'u':
                m->ut_verbose = false;
                m->ut_prefix = optarg;
                break;
            case 'U':
                m->ut_verbose = true;
                m->ut_prefix = optarg;
                break;
            case 'g':
                m->dont_fork = true;
                break;
            case 'l':
                m->ut_list_tests = true;
                break;
            case 'h':
            default:
                goto usage;
        }
    }
    owm_parse_non_option_arg(m, argc, argv);
    if (m->cli_arg != OWM_CLI_FAILURE) return;

usage:
    print_usage(argv);
    exit(EXIT_SUCCESS);
}

static void
owm_main_ut(struct owm_main *m)
{
    if (m->ut_list_tests == true) {
        osw_ut_print_test_names();
        exit(EXIT_SUCCESS);
    }

    if (m->ut_all == true) {
        const bool ok = osw_ut_run_all(m->dont_fork, m->ut_verbose);
        const int rv = (ok ? EXIT_SUCCESS : EXIT_FAILURE);
        exit(rv);
    }

    if (m->ut_prefix != NULL) {
        const bool ok = osw_ut_run_by_prefix(m->ut_prefix, m->dont_fork, m->ut_verbose);
        const int rv = (ok ? EXIT_SUCCESS : EXIT_FAILURE);
        exit(rv);
    }
}

static void
owm_main_run_get(struct owm_main *m)
{
    module_init();
    OSW_MODULE_LOAD(osw_etc);
    const char *value = osw_etc_get(m->cli_key);
    if (value != NULL)
        printf("%s\n", value);
    else
        exit(EXIT_FAILURE);
}

static void
owm_main_run_export_sh(void)
{
    module_init();
    OSW_MODULE_LOAD(osw_etc);
    osw_etc_export_sh();
}

static void
owm_main_run_cli(struct owm_main *m, char **argv)
{
    switch (m->cli_arg) {
        case OWM_CLI_NONE:
            return;
        case OWM_CLI_GET:
            owm_main_run_get(m);
            break;
        case OWM_CLI_EXPORT_SH:
            owm_main_run_export_sh();
            break;
        case OWM_CLI_FAILURE:
            print_usage(argv);
            break;
    }
    exit(EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
    struct owm_main *m = &g_owm_main;

    owm_main_parse_args(m, argc, argv);
    owm_main_run_cli(m, argv);
    ow_core_init(EV_DEFAULT);
    owm_main_ut(m);
    log_register_dynamic_severity(EV_DEFAULT);
    ow_core_run(EV_DEFAULT);
    return 0;
}
