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
#include <unistd.h>
#include <time.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdbool.h>
#include <syslog.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <jansson.h>

#include "log.h"
#include "os_time.h"
#include "target.h"
#include "assert.h"

#define LF '\n'
#define CR '\r'
#define NUL '\0'
#define LOGGER_BUFF_LEN (1024*8)
#define LOGGER_EXT_BUFF_LEN (3 * LOGGER_BUFF_LEN)
#define LIVE_LOGGING_PERIOD  5 /*seconds */

/**
 * Generate the module_table
 */

static log_module_entry_t log_module_table[LOG_MODULE_ID_LAST] =
{
    /** Expand the LOG_MODULE_TABLE by using the ENTRY macro above */
    #define LOG_ENTRY(mod)      { LOG_MODULE_ID_##mod, #mod, LOG_SEVERITY_DEFAULT },
    LOG_MODULE_TABLE(LOG_ENTRY)
};

/**
 * Elements must be in the same order as the log_severity_t enum
 */
static log_severity_entry_t log_severity_table[LOG_SEVERITY_LAST] =
{
    /** Expand the LOG_SEVERITY_TABLE by using the ENTRY macro above */
    #define COLOR_MAP(sev, color)   { LOG_SEVERITY_ ## sev,  #sev, LOG_COLOR_ ## color  },
    LOG_SEVERITY_TABLE(COLOR_MAP)
};


/* static global configuration */
static bool log_enabled              = false;

typedef struct
{
    bool        enabled;
    ev_stat     stat;
    const char *state_file_path;
    char        severity_string[LOG_SEVERITY_STR_MAX];
    const char *trigger_directory;
    int         trigger_value;
    void        (*trigger_callback)(FILE *fp);
} log_dynamic_t;


ds_dlist_t log_logger_list = DS_DLIST_INIT(logger_t, logger_node);

static log_dynamic_t log_dynamic;

static const char *log_name = ""; // process name

const char* log_get_name()
{
    return log_name;
}

/**
 * Severity per-module
 */
static void log_init(char *name)
{
    log_module_t mod;

    /* put all modules on default logging level */
    for (mod = 0; mod < (int)(sizeof(log_module_table) / sizeof (log_module_entry_t)); mod++)
    {
        log_module_table[mod].severity = LOG_SEVERITY_DEFAULT;
    }

    /* enable it */
    log_enabled  = true;

    log_name = name;

    memset(&log_dynamic, 0, sizeof(log_dynamic_t));
}

/**
 * See LOG_OPEN_ macros for flags, 0 or LOG_OPEN_DEFAULT will use the defaults (log to syslog and to
 * stdout (only if filedescriptor 0 is a TTY)
 */
bool log_open(char *name, int flags)
{
    log_init(name);

    openlog(name, LOG_NDELAY|LOG_PID, LOG_USER);

    /* Install default loggers */
    static logger_t logger_syslog;
    static logger_t logger_stdout;

    if ((flags == 0) || (flags & LOG_OPEN_DEFAULT))
    {
        flags |= LOG_OPEN_SYSLOG;
        if (isatty(0)) flags |= LOG_OPEN_STDOUT;
    }

    if (flags & LOG_OPEN_SYSLOG)
    {
        logger_syslog_new(&logger_syslog);
        log_register_logger(&logger_syslog);
    }

    if (flags & LOG_OPEN_STDOUT)
    {
        logger_stdout_new(&logger_stdout, flags & LOG_OPEN_STDOUT_QUIET);
        log_register_logger(&logger_stdout);
    }

    return true;
}

void log_register_logger(logger_t *logger)
{
    ds_dlist_insert_tail(&log_logger_list, logger);
}

void log_unregister_logger(logger_t *logger)
{
    ds_dlist_remove(&log_logger_list, logger);
}

/**
 * Map the severity name to the severty entry structure
 */
log_severity_entry_t *log_severity_get_by_name(char *name)
{
    log_severity_entry_t    *se;
    log_severity_t          s;

    for (s = 0;s < LOG_SEVERITY_LAST;s++) {
        se = &log_severity_table[s];
        if (!strcasecmp(se->name, name)) {
            return se;
        }
    }

    return NULL;
}
/**
 * Map the severity id to the severty entry structure
 */
log_severity_entry_t *log_severity_get_by_id(log_severity_t id)
{
    if (id >= LOG_SEVERITY_LAST) return NULL;

    return &log_severity_table[id];
}

void log_severity_set(log_severity_t s)
{
    log_module_t mod;

    LOG(DEBUG, "Log severity: %s", log_severity_str(s));

    /* The default log severity cannot be set to DEFAULT; demote it to INFO */
    if (s == LOG_SEVERITY_DEFAULT) s = LOG_SEVERITY_INFO;

    for (mod = 0; mod < (int)(sizeof(log_module_table) / sizeof (log_module_entry_t)); mod++)
    {
        /* set severity for all modules         */
        log_module_table[mod].severity = s;
    }
}

log_severity_t log_severity_get(void)
{
    return LOG_SEVERITY_DEFAULT;
}

void log_module_severity_set(log_module_t mod, log_severity_t sev)
{
    if (mod >= LOG_MODULE_ID_LAST)
    {
        return;
    }

    LOG(NOTICE, "Log severity %s: %s", log_module_str(mod), log_severity_str(sev));

    log_module_table[mod].severity = sev;
}

log_severity_t log_module_severity_get(log_module_t mod)
{
    if (mod >= LOG_MODULE_ID_LAST)
    {
        return LOG_SEVERITY_DEFAULT;
    }

    return log_module_table[mod].severity;
}

/**
 * Parse a severity string definition and set the severity levels accordingly.
 *
 * @p sevstr must be in the following formats
 *
 * [MODULE:]SEVERITY,[MODULE:]SEVERITY,...
 *
 * If MODULE is not specified, the default severity is modified.
 */
bool log_severity_parse(char *sevstr)
{
    char    psevstr[LOG_SEVERITY_STR_MAX];
    char   *tsevstr;
    char   *token;

    if (sevstr == NULL) return false;

    if (strlen(sevstr) >= LOG_SEVERITY_STR_MAX)
    {
        LOG(ERR, "Severity string is too long.");
        return false;
    }

    strncpy(psevstr, sevstr, sizeof(psevstr));

    token = strtok_r(psevstr, ", ", &tsevstr);

    while (token != NULL)
    {
        log_severity_t sev;
        log_module_t       mod;

        char *arg1 = NULL;
        char *arg2 = NULL;


        arg1 = token;
        arg2 = strchr(token, ':');

        if (arg2 == NULL)
        {
            /* If arg2 is NULL, it means we don't have a ':' string; this is the case where only the severity is specified in arg 1*/
            sev = log_severity_fromstr(arg1);

            if (sev != LOG_SEVERITY_LAST)
            {
                LOG(NOTICE, "Default severity set.::severity=%s", log_severity_str(sev));
                log_severity_set(sev);
                return true;
            }
            else
            {
                LOG(ERR, "Invalid default severity.::severity=%s", arg1);
                return false;
            }
        }
        else
        {
            /* String contains a colon. arg2 points to the ':' after the module name. Split the string,
             * so that arg1 points to the module string and arg2 points to the severity string */
            *arg2++ = '\0';

            mod = log_module_fromstr(arg1);
            sev = log_severity_fromstr(arg2);

            if (sev != LOG_SEVERITY_LAST && mod != LOG_MODULE_ID_LAST)
            {
                LOG(NOTICE, "Module log severity changed.::module=%s|severity=%s", log_module_str(mod), log_severity_str(sev));

                log_module_severity_set(mod, sev);
            }
            else
            {
                LOG(ERR, "Invalid module severity.::module=%s|severity=%s", arg1, arg2);
                return false;
            }
        }

        token = strtok_r(NULL, ", ", &tsevstr);
    }

    return true;
}

void log_close()
{
    LOG_MODULE_MESSAGE(NOTICE, LOG_MODULE_ID_COMMON, "log functionality closed");
    log_enabled = false;
}

void mlog(log_severity_t sev,
          log_module_t module,
          const char  *fmt, ...)
{
    char            buff[LOGGER_BUFF_LEN];
    char            timestr[80];
    struct tm             *lt;
    time_t                 t;
    va_list                args;
    char           *strip;
    log_severity_entry_t *se;
    char           *tag;
    log_severity_t    lsev;

    // Save errno, so that log does not overwrite it
    int save_errno = errno;

    if (false == log_enabled) {
        return;
    }

    if (module > LOG_MODULE_ID_LAST) module = LOG_MODULE_ID_MISC;

    lsev = log_module_table[module].severity;

    if (sev > lsev){
        return;
    }

    se = &log_severity_table[sev];
    tag = log_module_table[module].module_name;
    t = time_real();
    lt = localtime(&t);

    strftime(timestr, sizeof(timestr), "%d %b %H:%M:%S %Z", lt);

    // format
    va_start(args, fmt);
    vsnprintf(buff, sizeof(buff), fmt, args);
    va_end(args);

    // chop \r\n
    strip = &buff[strlen(buff) - 1];
    while ((strip > buff) && ((*strip == LF) || (*strip == CR)))
        *strip = NUL;

    // pretty print
    char se_tag[32];
    char *spaces = "          ";
    size_t se_len = 16;
    int scnt = 1;

    if ((strlen(se->name) + strlen(tag) + 2) < se_len) {
        scnt = se_len - (strlen(se->name) + strlen(tag) + 2);
    }

    snprintf(se_tag, sizeof(se_tag), "<%s>%.*s%s", se->name, scnt, spaces, tag);

    /* Craft the message */
    logger_msg_t msg;

    msg.lm_severity = sev;
    msg.lm_module = module;
    msg.lm_module_name = log_module_table[module].module_name;
    msg.lm_tag = se_tag;
    msg.lm_timestamp = timestr;
    msg.lm_text = buff;

    /* Feed messages to the registered loggers */
    logger_t *plog;
    ds_dlist_foreach(&log_logger_list, plog)
    {
        plog->logger_fn(plog, &msg);
    }

    // restore saved errno value
    errno = save_errno;

    return;
}

log_module_t log_module_fromstr(char *str)
{
    int ii;

    for (ii = 0; ii < LOG_MODULE_ID_LAST; ii++)
    {
        if (strcasecmp(str, log_module_table[ii].module_name) == 0)
        {
            return log_module_table[ii].module;
        }
    }

    return LOG_MODULE_ID_LAST;
}

log_severity_t log_severity_fromstr(char *str)
{
    int ii;

    for (ii = 0; ii < LOG_SEVERITY_LAST; ii++)
    {
        if (strcasecmp(str, log_severity_table[ii].name) == 0)
        {
            return log_severity_table[ii].s;
        }
    }

    return LOG_SEVERITY_LAST;
}

char *log_module_str(log_module_t mod)
{
    if (mod >= LOG_MODULE_ID_LAST) mod = LOG_MODULE_ID_MISC;

    return log_module_table[mod].module_name;
}

char *log_severity_str(log_severity_t sev)
{
    if (sev >= LOG_SEVERITY_LAST) sev = LOG_SEVERITY_DEFAULT;

    return log_severity_table[sev].name;
}

bool log_isenabled()
{
    return log_enabled;
}

/**
 * Dynamic logging state handling
 */

static bool log_dynamic_state_read(int *new_trigger, char *new_severity, int new_severity_len)
{
    *new_trigger = 0;
    *new_severity = '\0';

    LOGT("Re-reading logging state file %s.", log_dynamic.state_file_path);

    json_t *loggers_json = json_load_file(log_dynamic.state_file_path, 0, NULL);
    if (!loggers_json) {
        LOGD("Unable to read dynamic log state!");
        return false;
    }

    // Get settings for our logger if there is any.
    json_t *logger = json_object_get(loggers_json, log_name);
    if (!logger) {
        // If process settings don't exist, use "DEFAULT" entry
        logger = json_object_get(loggers_json, LOG_DEFAULT_ENTRY);
        if (!logger) {
            // No setting found
            json_decref(loggers_json);
            return false;
        }
    }

    // Get new severity
    json_t *severity = json_object_get(logger, "log_severity");
    if (severity && json_is_string(severity))
    {
        strlcpy(new_severity,
                json_string_value(severity),
                new_severity_len);
    }

    // Get new trigger
    json_t *trigger = json_object_get(logger, "log_trigger");
    if (trigger && json_is_integer(trigger))
    {
        *new_trigger = json_integer_value(trigger);
    }

    json_decref(loggers_json);

    return true;
}

static void log_dynamic_full_path_get(char *buf, int len)
{
    time_t now;
    struct tm ts;
    time(&now);
    ts = *localtime(&now);

    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y%m%d-%H%M%S", &ts);

    // Create full path
    //   <directory>/<name>-<timestamp>.dump
    snprintf(buf, len, "%s/%s-%s.dump",
             log_dynamic.trigger_directory,
             log_name,
             timestamp);
}

static void log_dynamic_state_handler(struct ev_loop *loop,
                                      ev_stat *watcher,
                                      int revents)
{
    int     new_trigger;
    char    new_severity[LOG_SEVERITY_STR_MAX];
    if (!log_dynamic_state_read(&new_trigger, new_severity, sizeof(new_severity)))
    {
        return;
    }

    // Update severity in case it was changed
    if (new_severity[0] && strcmp(new_severity, log_dynamic.severity_string))
    {
        LOGN("Module log severity changed from \"%s\" to \"%s\".",
             log_dynamic.severity_string, new_severity);

        if (!log_severity_parse(new_severity))
        {
            LOGE("Failed to update log severity!");
        }

        strlcpy(log_dynamic.severity_string,
                new_severity,
                sizeof(log_dynamic.severity_string));
    }

    // Trigger logging callback in case it was requested. Logging
    // callback expects file pointer and should dump information
    // in this file.
    if (log_dynamic.trigger_callback &&
        log_dynamic.trigger_directory &&
        new_trigger &&
        new_trigger > log_dynamic.trigger_value)
    {
        char full_path[LOG_TRIGGER_DIR_MAX * 2];
        log_dynamic_full_path_get(full_path, sizeof(full_path));

        LOGN("Module requested to dump information into %s", full_path);

        FILE *fp = fopen(full_path, "w");
        if (fp) {
            log_dynamic.trigger_callback(fp);
            fclose(fp);
        } else {
            LOGE("Unable to open file %s for dumping information! [%s]",
                 full_path, strerror(errno));
        }
    }

    // Update global values
    strlcpy(log_dynamic.severity_string,
            new_severity,
            sizeof(log_dynamic.severity_string));

    log_dynamic.trigger_value = new_trigger;

}

static void log_dynamic_handler_init(struct ev_loop *loop)
{
    if (!log_dynamic.enabled)
    {
        log_dynamic.state_file_path = target_log_state_file();
        if (!log_dynamic.state_file_path)
        {
            // On this target we don't enable dynamic log handler
            return;
        }

        // Init local state
        int   new_trigger;
        char  new_severity[LOG_SEVERITY_STR_MAX];
        log_dynamic_state_read(&new_trigger, new_severity, sizeof(new_severity));

        log_dynamic.enabled           = true;
        log_dynamic.trigger_value     = new_trigger;
        log_dynamic.trigger_directory = target_log_trigger_dir();

        strlcpy(log_dynamic.severity_string,
                new_severity,
                sizeof(log_dynamic.severity_string));

        // Init severity
        if (new_severity[0])
        {
            if (!log_severity_parse(new_severity))
            {
                LOGE("Failed to set initial dynamic log severity!");
            }
        }

        // Init timer
        ev_stat_init(&log_dynamic.stat,
                     log_dynamic_state_handler,
                     log_dynamic.state_file_path,
                     1.0);
        ev_stat_start(loop, &log_dynamic.stat);
    }
}

/**
 * Register your logger to dynamic log severity updates.
 */
bool log_register_dynamic_severity(struct ev_loop *loop)
{
    log_dynamic_handler_init(loop);
    return true;
}

/**
 * Register your logger to dynamic data dump trigger callbacks.
 */
bool log_register_dynamic_trigger(struct ev_loop *loop, void (*callback)(FILE *fp))
{
    log_dynamic.trigger_callback = callback;

    log_dynamic_handler_init(loop);
    return true;
}

/**
 * Sets dynamic log severity. Note that this function can be used by
 * short running programs to set dynamic log severity during runtime.
 */
bool log_severity_dynamic_set()
{
    log_dynamic.state_file_path = target_log_state_file();
    if (!log_dynamic.state_file_path)
    {
        // On this target we don't enable dynamic log handler
        return false;
    }

    int  new_trigger; // Not used
    char new_severity[LOG_SEVERITY_STR_MAX];
    if (!log_dynamic_state_read(&new_trigger, new_severity, sizeof(new_severity)))
    {
        return false;
    }

    if (!log_severity_parse(new_severity))
    {
        LOGW("Failed to parse dynamic log severity!");
    }

    return true;
}
