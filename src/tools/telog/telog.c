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
#include <string.h>

#include <log.h>
#include <timevt_client.h>

#define ECAT_DEFAULT "GENERAL"

enum e_opt_id
{
	OPT_ADDR, OPT_NAME, OPT_CAT, OPT_SUB, OPT_STEP, OPT_HELP, OPT_VER, OPT_LOCAL, OPT_STDOUT
};

typedef struct sOption
{
	enum e_opt_id id;
	int params; // option expected params
	const char *opt_short;
	const char *opt_long;
	const char *name;
} tOption;

static const tOption Options[] =
{
	{ .id = OPT_ADDR, .params = 1, .opt_short = "-a", .opt_long = "--addr <addr>", .name = "Server address path, default is "TESRV_SOCKET_ADDR },
	{ .id = OPT_NAME, .params = 1, .opt_short = "-n", .opt_long = "--name <name>", .name = "Client name, default is process name" },
	{ .id = OPT_CAT,  .params = 1, .opt_short = "-c", .opt_long = "--category <cat>", .name = "Event category, default is "ECAT_DEFAULT },
	{ .id = OPT_SUB,  .params = 1, .opt_short = "-s", .opt_long = "--subject <sub>", .name = "Event subject name, optional" },
	{ .id = OPT_STEP, .params = 1, .opt_short = "-t", .opt_long = "--step <step>", .name = "Event step name, default is "TECLI_DEFAULT_STEP },
	{ .id = OPT_LOCAL,.params = 0, .opt_short = "-l", .opt_long = "--local", .name = "Only local logging w/o server connection" },
    {.id = OPT_STDOUT,.params = 0, .opt_short = "-o", .opt_long = "--stdout", .name = "Log to stdout instead of default syslog" },
	{ .id = OPT_HELP, .params = 0, .opt_short = "-h", .opt_long = "--help", .name = "Prints help message for logger usage and exits" },
	{ .id = OPT_VER,  .params = 0, .opt_short = "-v", .opt_long = "--version", .name = "Prints time-event logger version and exits" },

};

static const tOption *find_opt(const char *opt_str)
{
	size_t n;
	for (n = 0; n < sizeof(Options)/sizeof(Options[0]); n++)
	{
		if (0 == strcmp(opt_str, Options[n].opt_short)
		|| 0 == strcmp(opt_str, Options[n].opt_long))
		{
			return &Options[n];
		}
	}
	return NULL;
}

static void print_logger_version()
{
	puts("PLUME time-event logger utility, version 1.0");
	puts("Enter time-events to the logging server and locally\n");
}

static void print_help(const char *appname)
{
	print_logger_version();
	printf("Usage: %s [options] [message]\n\n", appname);
	puts("Available options:");
	size_t n;
	for ( n = 0; n < sizeof(Options)/sizeof(Options[0]); n++ )
	{
		printf("%s, %-16s : %s\n", Options[n].opt_short, Options[n].opt_long, Options[n].name);
	}
	puts("");
}

int main(int argc, char *argv[])
{
	int rv = EXIT_FAILURE;

	const char *cat = ECAT_DEFAULT;
	const char *addr = TESRV_SOCKET_ADDR;
	char *name = argv[0];

	const char *subject = NULL;
	const char *step = NULL;
	const char *msg = NULL;
	int local_log = 0;
	int stdout_log = 0;

	int n;
	for (n = 1; n < argc; n++)
	{
		const char *arg = argv[n];

		if(arg[0] == '-')
		{ // this is option

			const tOption * opt = find_opt(arg);
			if (opt != NULL && n < argc - opt->params)
			{
				switch(opt->id)
				{
				case OPT_ADDR: addr = argv[++n]; break;
				case OPT_NAME: name = argv[++n]; break;
				case OPT_CAT:  cat = argv[++n]; break;
				case OPT_SUB:  subject = argv[++n]; break;
				case OPT_STEP: step = argv[++n]; break;

				case OPT_LOCAL: local_log = 1; break;
				case OPT_STDOUT: stdout_log = 1; break;

				case OPT_VER:
					print_logger_version();
					return EXIT_SUCCESS;

				case OPT_HELP:
					print_help(argv[0]);
					return EXIT_SUCCESS;
				}
			}
		}
		else if (n == argc - 1)
		{ // this is last arg : treat it as a message
			msg = arg;
		}
	}

	log_open(name, stdout_log ? LOG_OPEN_STDOUT : LOG_OPEN_SYSLOG);

	if (te_client_init(name, local_log ? NULL : addr))
	{
		if (te_client_log(cat, subject, step, msg ? "%s" : NULL, msg))
		{
			rv = EXIT_SUCCESS;
		}
		else
		{
			fprintf(stderr, "ERR: te-client remote logging failed\n");
		}
		te_client_deinit();
	}
	else
	{
		fprintf(stderr, "ERR: te-client init failure\n");
	}
	return rv;
}
