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
 * logread-osync.c
 *
 * OpenSync's version of logread utility
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>
#include <stdarg.h>
#include <ev.h>
#include <jansson.h>

#include "log.h"
#include "target.h"
#include "tailf.h"
#include "memutil.h"

/***************************************************************************************/

#define VERSION "2.0"

#ifdef TARGET_LOGREAD_FILENAME
#define DEFAULT_FILENAME TARGET_LOGREAD_FILENAME
#else
#define DEFAULT_FILENAME "/var/log/messages"
#endif

/***************************************************************************************/

typedef enum
{
    FILTER_TYPE_PROC = 0,
    FILTER_TYPE_PID,
    FILTER_TYPE_SEVERITY,
    FILTER_TYPE_MODULE,
    FILTER_TYPE_SEARCH,
    FILTER_TYPE_MAX
} filter_type_t;

typedef struct filter_ent
{
    bool invert;
    bool sensitive;
    char *match;
    struct filter_ent *next;
} filter_ent_t;

typedef struct
{
    filter_ent_t *entries[FILTER_TYPE_MAX];
} filter_t;

/***************************************************************************************/

filter_t *filter = NULL;
tailf_t tf;
json_t *json_base = NULL;
uint32_t line_num = 0;
bool color_output = true;
bool force_color = false;
bool json_output = false;
bool line_numbers = false;

/***************************************************************************************/

filter_t *filter_alloc(void)
{
    filter_t *fp;

    fp = CALLOC(1, sizeof(*filter));
    return fp;
}

int filter_add(filter_t *fp, filter_type_t type, char *match, bool invert, bool sensitive)
{
    filter_ent_t *ent;

    if (!match || type >= FILTER_TYPE_MAX)
    {
        return -1;
    }

    // Handy shortcut
    if (type == FILTER_TYPE_SEVERITY && !strcasecmp(match, "warn"))
    {
        match = "warning";
    }

    ent = MALLOC(sizeof(*ent));

    ent->invert = invert;
    ent->sensitive = sensitive;
    ent->match = strdup(match);
    ent->next = fp->entries[type];
    fp->entries[type] = ent;
    return (0);
}

void filter_free(filter_t *fp)
{
    filter_type_t type;
    filter_ent_t *cur, *next;

    if (fp)
    {
        for (type = 0; type < FILTER_TYPE_MAX; type++)
        {
            if ((cur = fp->entries[type]))
            {
                while (cur)
                {
                    next = cur->next;
                    if (cur->match)
                    {
                        FREE(cur->match);
                    }
                    FREE(cur);
                    cur = next;
                }
            }
        }
        FREE(fp);
    }
}

bool filter_match(filter_t *fp, filter_type_t type, const char *what)
{
    filter_ent_t *ent;

    if ((ent = fp->entries[type]) == NULL)
    {
        // Not filtering on this type, return match
        return true;
    }

    if (!what)
    {  // in case NULL was passed in
        return false;
    }

    while (ent)
    {
        if (type == FILTER_TYPE_SEARCH)
        {
            if (ent->sensitive)
            {
                if (strstr(what, ent->match))
                {
                    if (!ent->invert)
                    {
                        return true;  // sensitive match
                    }
                }
                else if (ent->invert)
                {
                    return true;  // invert match
                }
            }
            else if (strcasestr(what, ent->match))
            {
                if (!ent->invert)
                {
                    return true;  // insensitive match
                }
            }
            else if (ent->invert)
            {
                return true;  // invert match
            }
        }
        else
        {
            // Must match exactly, case insensitive for other types
            if (!strcasecmp(what, ent->match))
            {
                if (!ent->invert)
                {
                    return true;  // match
                }
            }
            else if (ent->invert)
            {
                return true;  // invert match
            }
        }

        ent = ent->next;
    }
    return false;  // no match
}

void process_line(const char *line)
{
    log_severity_entry_t *se;
    const char *msg;
    json_t *json;
    char buf[128];
    char *copy = strdup(line), *jstr;
    char *m, *d, *tm, *proc, *sev, *mod, *pid;
    char *sptr, *e;
    char *color_start = "", *color_end = "";
#if defined(CONFIG_LOGREAD_OSYNC_LOG_HAS_YEAR)
    char *yr;
#endif /* defined(CONFIG_LOGREAD_OSYNC_LOG_HAS_YEAR) */
#if defined(CONFIG_LOGREAD_OSYNC_LOG_HAS_LEVEL)
    char *llvl;
#endif /* defined(CONFIG_LOGREAD_OSYNC_LOG_HAS_LEVEL) */
#if defined(CONFIG_LOGREAD_OSYNC_LOG_HAS_HOSTNAME)
    char *hn;
#endif /* defined(CONFIG_LOGREAD_OSYNC_LOG_HAS_HOSTNAME) */

    line_num++;
    do
    {
        // Parse common tokens from log output.  If missing, skip line
        if (!(m = strtok_r(copy, " \t", &sptr))) break;   // Month
        if (!(d = strtok_r(NULL, " \t", &sptr))) break;   // Day
        if (!(tm = strtok_r(NULL, " \t", &sptr))) break;  // Time
#if defined(CONFIG_LOGREAD_OSYNC_LOG_HAS_YEAR)
        if (!(yr = strtok_r(NULL, " \t", &sptr))) break;  // Year
#endif                                                    /* defined(CONFIG_LOGREAD_OSYNC_LOG_HAS_YEAR) */
#if defined(CONFIG_LOGREAD_OSYNC_LOG_HAS_LEVEL)
        if (!(llvl = strtok_r(NULL, " \t", &sptr))) break;  // Level
#endif                                                      /* defined(CONFIG_LOGREAD_OSYNC_LOG_HAS_LEVEL) */
#if defined(CONFIG_LOGREAD_OSYNC_LOG_HAS_HOSTNAME)
        if (!(hn = strtok_r(NULL, " \t", &sptr))) break;    // Hostname
#endif                                                      /* defined(CONFIG_LOGREAD_OSYNC_LOG_HAS_HOSTNAME) */
        if (!(proc = strtok_r(NULL, " \t", &sptr))) break;  // Proccess

        // save msg pointer for later, in case of JSON output
        msg = line + (strlen(line) - strlen(sptr));

        // try to parse custom severity and module tokens
        sev = strtok_r(NULL, " \t", &sptr);
#if defined(CONFIG_LOG_USE_PREFIX)
        if (sev && !strcmp(sev, CONFIG_LOG_PREFIX))
        {
            // Skip the log prefix
            sev = strtok_r(NULL, " \t", &sptr);
        }
#endif
        mod = strtok_r(NULL, " \t", &sptr);

        if (sev && mod)
        {
            if (sev[0] == '<' && sev[strlen(sev) - 1] == '>' && mod[strlen(mod) - 1] == ':')
            {
                // yep, it's our custom severity and module tokens
                sev++;
                sev[strlen(sev) - 1] = '\0';
                mod[strlen(mod) - 1] = '\0';

                // reset msg pointer to exclude severity and module
                msg = line + (strlen(line) - strlen(sptr));
            }
            else
            {
                sev = mod = NULL;
            }
        }
        else
        {
            sev = mod = NULL;
        }

        // check if process includes a pid number
        if ((e = strchr(proc, '[')))
        {
            *e = '\0';
            pid = e + 1;
            if ((e = strchr(pid, ']')))
            {
                *e = '\0';
            }
            else
            {
                pid = NULL;
            }
        }
        else if (proc[strlen(proc) - 1] == ':')
        {
            proc[strlen(proc) - 1] = '\0';
            pid = NULL;
        }
        else
        {
            sev = mod = pid = NULL;
        }

        // check if this line matches filters
        if (!filter_match(filter, FILTER_TYPE_PROC, proc) || !filter_match(filter, FILTER_TYPE_PID, pid)
            || !filter_match(filter, FILTER_TYPE_SEVERITY, sev) || !filter_match(filter, FILTER_TYPE_MODULE, mod)
            || !filter_match(filter, FILTER_TYPE_SEARCH, line))
        {
            break;
        }

        if (line_numbers)
        {
            printf("%d:", line_num);
        }

        if (json_output)
        {  // JSON output for elasticsearch
            if (json_base)
            {
                json = json_copy(json_base);
            }
            else
            {
                json = json_object();
            }
#if defined(CONFIG_LOGREAD_OSYNC_LOG_HAS_YEAR)
            snprintf(buf, sizeof(buf) - 1, "%s %s %s %s", m, d, tm, yr);  // re-create timestamp
#else  /* not defined(CONFIG_LOGREAD_OSYNC_LOG_HAS_YEAR) */
            snprintf(buf, sizeof(buf) - 1, "%s %s %s", m, d, tm);  // re-create timestamp
#endif /* not defined(CONFIG_LOGREAD_OSYNC_LOG_HAS_YEAR) */
            json_object_set_new(json, "@version", json_integer(1));
            json_object_set_new(json, "message_tm", json_string(buf));
            if (proc) json_object_set_new(json, "process", json_string(proc));
            if (pid) json_object_set_new(json, "pid", json_integer(atoi(pid)));
            if (sev) json_object_set_new(json, "severity", json_string(sev));
            if (mod) json_object_set_new(json, "module", json_string(mod));
            json_object_set_new(json, "message", json_string(msg));

            // print JSON formatted line
            if ((jstr = json_dumps(json, JSON_PRESERVE_ORDER)))
            {
                printf("%s\n", jstr);
                FREE(jstr);
            }
            json_decref(json);
        }
        else
        {
            if (sev && color_output)
            {
                // find color for message
                se = log_severity_get_by_name(sev);
                if (se && se->color != LOG_COLOR_NONE && (isatty(1) || force_color))
                {
                    color_start = se->color;
                    color_end = LOG_COLOR_NORMAL;
                }
            }

            // print line
            printf("%s%s%s\n", color_start, line, color_end);
        }
        fflush(stdout);
    } while (0);

exit:
    FREE(copy);
}

int parse_lines(char *buf, int len)
{
    char *start = buf, *next, *eol;
    int llen = 0;

    // Process full lines only.  Partial lines will be processed on next call
    while ((eol = strchr(start, '\n')))
    {
        next = eol + 1;
        *eol = '\0';

        process_line(start);

        len -= (next - start);
        llen += (next - start);
        if (len <= 0)
        {
            break;
        }
        start = next;
    }

    return llen;
}

int read_file(char *file)
{
    char buf[4096 + 1];
    size_t len = 0, rlen, llen;
    int fd;

    // Could use fopen/fgets here, but this method helps
    // test the parse_lines() ability to handle partials

    if ((fd = open(file, O_RDONLY)) <= 0)
    {
        fprintf(stderr, "ERROR: Failed to open file \"%s\".  Please use --file for different file\n", file);
        return -1;
    }

    // Read chunk into buffer past the previously saved partial line
    while ((rlen = read(fd, &buf[len], (sizeof(buf) - 1) - len)) > 0)
    {
        rlen += len;
        buf[rlen] = '\0';
        llen = parse_lines(buf, rlen);
        len = (rlen - llen);
        if (len > 0)
        {
            // Move partial line left to top of buffer
            memcpy(&buf, &buf[llen], len);
        }
    }

    close(fd);
    return (0);
}

void tailf_cbk(struct ev_loop *loop, void *ev, int revents)
{
    static char buf[2048 + 1];
    static size_t len = 0, rlen, llen;

    // Read chunk into buffer past the previously saved partial line
    while ((rlen = tailf_read(&tf, &buf[len], (sizeof(buf) - 1) - len)) > 0)
    {
        rlen += len;
        buf[rlen] = '\0';
        llen = parse_lines(buf, rlen);
        len = (rlen - llen);
        if (len > 0)
        {
            // Move partial line left to top of buffer
            memcpy(&buf, &buf[llen], len);
        }
    }
}

void process_filter(filter_type_t type, char *str, bool invert, bool sensitive)
{
    char *what = str, *next;
    int ret;

    while ((next = strchr(what, ',')))
    {
        *next++ = '\0';
        ret = filter_add(filter, type, what, invert, sensitive);
        if (ret < 0)
        {
            goto error_exit;
        }
        what = next;
    }

    ret = filter_add(filter, type, what, invert, sensitive);
error_exit:
    if (ret < 0)
    {
        fprintf(stderr, "ERROR adding filter \"%s\"\n", what);
        exit(1);
    }
}

void usage(char *prog)
{
    fprintf(stderr, "OpenSync logread v%s\n", VERSION);
    fprintf(stderr, "Usage: %s [options]\n\n", prog);
    fprintf(stderr, "Options Available:\n");
    fprintf(stderr, "   -V|--version                   Print version information\n");
    fprintf(stderr, "   -F|--file <filename>           Read from file insted of %s\n", DEFAULT_FILENAME);
    fprintf(stderr, "   -p|--process <name>,...        Filter for process name(s)\n");
    fprintf(stderr, "   -P|--pid <PID>,...             Filter for process ID(s)\n");
    fprintf(stderr, "   -s|--severity <level>,...      Filter for severity level(s)\n");
    fprintf(stderr, "   -m|--module <module>,...       Filter for module(s)\n");
    fprintf(stderr, "   -S|--search <text>,...         Filter for text\n");
    fprintf(stderr, "   -i|--insensitive               Case insensitive searching\n");
    fprintf(stderr, "   -v|--invert                    Invert matching\n");
    fprintf(stderr, "   -f|--follow                    Follow file (tail -f behavior)\n");
    fprintf(stderr, "   -C|--no-color                  Disable color output\n");
    fprintf(stderr, "   -c|--force-color               Force color output even if not tty\n");
    fprintf(stderr, "   -a|--all                       Display all contents (with -f)\n");
    fprintf(stderr, "   -n|--line-numbers              Display Line Numbers\n");
    fprintf(stderr, "   -j|--json                      Output logs in JSON format\n");
    fprintf(stderr, " * -J|--json-add <json string>    Add JSON to each line output\n");
    fprintf(stderr, " * -T|--token <token=value>       Add a token to JSON output\n");
    fprintf(stderr, "\n* Implies -j/--json to enable JSON output\n");
    exit(1);
}

int main(int argc, char **argv)
{
    json_error_t json_err;
    ev_timer evt;
    char *file = DEFAULT_FILENAME;
    char *t, *v;
    bool follow = false;
    bool invert = false;
    bool sensitive = true;
    bool all = false;
    int opt;
    int ret = 0;

    struct option long_opts[] = {
        {"version", no_argument, NULL, 'V'},
        {"process", required_argument, NULL, 'p'},
        {"pid", required_argument, NULL, 'P'},
        {"severity", required_argument, NULL, 's'},
        {"module", required_argument, NULL, 'm'},
        {"search", required_argument, NULL, 'S'},
        {"file", required_argument, NULL, 'F'},
        {"insensitive", no_argument, NULL, 'i'},
        {"invert", no_argument, NULL, 'v'},
        {"follow", no_argument, NULL, 'f'},
        {"no-color", no_argument, NULL, 'C'},
        {"force-color", no_argument, NULL, 'c'},
        {"all", no_argument, NULL, 'a'},
        {"line-numbers", no_argument, NULL, 'n'},
        {"json", no_argument, NULL, 'j'},
        {"json-add", required_argument, NULL, 'J'},
        {"token", required_argument, NULL, 'T'},
        {NULL, no_argument, NULL, 0}};
    char *opts = "Vp:P:s:m:S:F:ivfCcanjJ:T:";

    assert((filter = filter_alloc()));

    while ((opt = getopt_long(argc, argv, opts, long_opts, NULL)) >= 0)
    {
        switch (opt)
        {
            case 'V':
                printf("OpenSync logread v%s\n", VERSION);
                exit(0);

            case 'p':
                process_filter(FILTER_TYPE_PROC, optarg, invert, sensitive);
                break;

            case 'P':
                process_filter(FILTER_TYPE_PID, optarg, invert, sensitive);
                break;

            case 's':
                process_filter(FILTER_TYPE_SEVERITY, optarg, invert, sensitive);
                break;

            case 'm':
                process_filter(FILTER_TYPE_MODULE, optarg, invert, sensitive);
                break;

            case 'S':
                process_filter(FILTER_TYPE_SEARCH, optarg, invert, sensitive);
                break;

            case 'F':
                file = optarg;
                break;

            case 'i':
                sensitive = false;
                break;

            case 'v':
                invert = true;
                break;

            case 'f':
                follow = true;
                break;

            case 'C':
                color_output = false;
                break;

            case 'c':
                color_output = true;
                force_color = true;
                break;

            case 'a':
                all = true;
                break;

            case 'n':
                line_numbers = true;
                break;

            case 'j':
                json_output = true;
                break;

            case 'J':
                if (json_base)
                {
                    fprintf(stderr, "ERROR: -J/--json-add provided twice or after -T/--token\n");
                    exit(1);
                }
                json_output = true;
                json_base = json_loads(optarg, 0, &json_err);
                if (!json_base)
                {
                    fprintf(stderr, "ERROR: Failed to parse JSON-add stirng (%s)\n", json_err.text);
                    exit(1);
                }
                break;

            case 'T':
                t = strtok(optarg, "=");
                v = strtok(NULL, "=");
                if (!t || !v)
                {
                    fprintf(stderr, "ERROR: -T/--token must be in format of TOKEN=VALUE\n");
                    exit(1);
                }
                json_output = true;
                if (!json_base)
                {
                    json_base = json_object();
                }
                json_object_set_new(json_base, t, json_string(v));
                break;

            default:
                usage(argv[0]);
        }
    }

    if (all || !follow)
    {
        // Display entire contents of file
        ret = read_file(file);
    }

    if (follow && ret == 0)
    {
        // Follow file for future changes
        tailf_open(&tf, file);

        ev_timer_init(&evt, (void *)tailf_cbk, 0.1, 0.1);
        ev_timer_start(EV_DEFAULT, &evt);
        ev_run(EV_DEFAULT, 0);

        tailf_close(&tf);
    }

    filter_free(filter);
    if (json_base)
    {
        json_decref(json_base);
    }
    exit((ret == 0) ? 0 : 1);
}
