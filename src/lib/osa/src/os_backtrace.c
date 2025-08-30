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

/**
 * This library is GCC specific.
 *
 * To get the full benefits of the backtrace library, the code must be
 * compiled with the following flags: `-fasynchronous-unwind-tables -rdynamic`
 *
 * Depending on optimization settings (and compiler version), option
 * `-fomit-frame-pointer` may get set by default, which also affects stack
 * dumps, potentially making them useless (be it in the backtrace library,
 * or when examining coredump files). This can be reverted by explicitly
 * adding flag `-fno-omit-frame-pointer`.
 */

/* Need this to get dladdr() */
#define _GNU_SOURCE

#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <kconfig.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <fcntl.h>

#if defined(WITH_LIBGCC_BACKTRACE)
#include <unwind.h>
#endif

#include "os_common.h"
#include "os_time.h"
#include "os_proc.h"
#include "osp_unit.h"
#include "util.h"

#include "log.h"
#include "os.h"
#include "os_backtrace.h"
#include "os_file_ops.h"
#include "target.h"

#if defined(WITH_LIBGCC_BACKTRACE)

#define MODULE_ID  LOG_MODULE_ID_COMMON

#define BACKTRACE_MAX_ITERATIONS 20
#define BACKTRACE_CRASH_TIMEOUT 3
#define FILENAME_MEM_MAX_PREFIX "mem_max_"


/**
 * Stack-trace functions, mostly inspired from libubacktrace
 */
void                            os_backtrace_sig_crash(int signum);
static os_backtrace_func_t      os_backtrace_dump_cbk;

/* NOTE: using fd instead of FILE* because FILE apis are not signal safe */

/* fd_crash_log is peristently stored in INSTALL_PREFIX/log_archive/crash */
static int                      fd_crash_log = -1;
/* fd_crash_report goes to /tmp is sent via mqtt and is then removed */
static int                      fd_crash_report = -1;

static int g_crash_signum;
static struct sigaction g_save_alarm_sa;
static int g_save_alarm_time;

#define VALID_FD(FD) ((FD) >= 0)
#define NOT_AVAILABLE_STR "<NA>"

/**
 * Install crash handlers that dump the current stack in the log file
 */
void os_backtrace_init(void)
{
    struct sigaction sa;

    sigemptyset(&sa.sa_mask);
    sa.sa_handler   = os_backtrace_sig_crash;
    sa.sa_flags     = SA_RESETHAND | SA_NODEFER;
    /*
     * These two flags allow us to execute the default signal handler by
     * using raise from our handler or from the alarm handler:
     *
     * SA_RESETHAND
     * Restore the signal action to the default upon entry to the signal handler.
     *
     * SA_NODEFER
     * Do not add the signal to the signal mask while the handler is executing.
     */

    sigaction(SIGABRT, &sa, NULL);
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGILL , &sa, NULL);
    sigaction(SIGFPE , &sa, NULL);
    sigaction(SIGBUS , &sa, NULL);

    /* SIGUSR2 is used just for stack reporting, while SIGINT and SIGTERM are forwarded to the child */
    sigemptyset(&sa.sa_mask);
    sa.sa_handler   = os_backtrace_sig_crash;
    sa.sa_flags     = 0;

    sigaction(SIGUSR2, &sa, NULL);
}

/**
 * Alarm handler which is called if the crash_report hangs
 */
void os_backtrace_alarm_handler(int signum)
{
    if (g_crash_signum != SIGUSR2) {
        // raise original signal to handle the crash
        raise(g_crash_signum);
    } else {
        // default USR2 handler does not exit so raise ABORT instead,
        // but first reset ABORT to default handler
        struct sigaction sa;
        sigemptyset(&sa.sa_mask);
        sa.sa_handler = SIG_DFL; // default action
        sa.sa_flags = 0;
        sigaction(SIGABRT, &sa, NULL);
        raise(SIGABRT);
    }
    // this should not be reached, exit with error
    exit(128 + g_crash_signum);
}

/**
 * Set an alarm handler and start the timer which will
 * be triggered if the crash_report hanghs
 */
void os_backtrace_start_alarm()
{
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = os_backtrace_alarm_handler;
    sa.sa_flags = 0;
    sigaction(SIGALRM, &sa, &g_save_alarm_sa);
    g_save_alarm_time = alarm(BACKTRACE_CRASH_TIMEOUT);
}

/**
 * Restore previous alarm handler and time
 */
void os_backtrace_reset_alarm()
{
    sigaction(SIGALRM, &g_save_alarm_sa, NULL);
    alarm(g_save_alarm_time);
}

/**
 * The crash handler
 */
void os_backtrace_sig_crash(int signum)
{
    g_crash_signum = signum;
    os_backtrace_start_alarm();
    LOG(ALERT, "Signal %d received, generating stack dump...\n", signum);

    sig_crash_report(signum);

    if (signum != SIGUSR2)
    {
        /* At this point the handler for this signal was reset (except for SIGUSR2) due to the SA_RESETHAND flag;
           so re-send the signal to ourselves in order to properly crash */
        raise(signum);
    }
    os_backtrace_reset_alarm();
}

/**
 * Log current stack trace
 */
struct os_backtrace_dump_info
{
    int frame_no;
    char addr_info[512];
};

/* printf to a fd */
static int fd_printf(int fd, char *fmt, ...)
{
    va_list args;
    int len = -1;
    int ret = -1;

    if (fd < 0) return -1;
    va_start(args, fmt);
    len = vsnprintf(NULL, 0, fmt, args);
    va_end(args);
    if (len < 0) return len;
    char buf[len + 1];
    va_start(args, fmt);
    len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (len < 0) return -1;
    if (len >= (int)sizeof(buf)) return -1;
    ret = write(fd, buf, len);
    if (ret != len) return -1;

    return len;
}

static void crash_print(char *fmt, ...)
{
    char    buf[BFR_SIZE_512];
    va_list args;

    memset(buf, 0x00, sizeof(buf));
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (VALID_FD(fd_crash_log))
    {
        fd_printf(fd_crash_log, "%s\n", buf);
    }

    LOG(ERR, "%s", buf);
}

static void crash_report_print(char *line)
{
    if (VALID_FD(fd_crash_report))
    {
        fd_printf(fd_crash_report, "%s\\n", line);
    }
}

void os_backtrace_dump_generic(btrace_type btrace)
{
    struct os_backtrace_dump_info di;

    if (btrace != BTRACE_LOG_ONLY) {
        /* This call opens up a file </location/crashed_<process_name>_<timestamp>.pid> */
        fd_crash_log = os_file_open_fd(CONFIG_OS_BACKTRACE_DUMP_PATH, "crashed");
    }

    int addr_len = 2 + sizeof(void*) * 2;
    crash_print("====================== STACK TRACE =================================");
    crash_print("FRAME %*s: %26s %-6s %s", addr_len, "ADDR", "FUNCTION", "OFFSET", "OBJECT");
    crash_print("--------------------------------------------------------------------");

    di.frame_no = 0;
    di.addr_info[0] = '\0';
    os_backtrace(os_backtrace_dump_cbk, &di);

    crash_print("====================================================================");
    crash_print("Note: Use the following command line to get a full trace:");
    crash_print("addr2line -e DEBUG_BINARY -ifp %s", di.addr_info);

    if (VALID_FD(fd_crash_log)) {
        close(fd_crash_log);
        fd_crash_log = -1;
    }
}


void sig_crash_report_mem_max(pid_t pid, uint32_t pss)
{
    char template[128] = CONFIG_DM_CRASH_REPORTS_TMP_DIR "/" FILENAME_MEM_MAX_PREFIX "XXXXXX";
    int fd;

    if (kconfig_enabled(CONFIG_DM_OSYNC_CRASH_REPORTS))
    {
        mkdir(CONFIG_DM_CRASH_REPORTS_TMP_DIR, 0755);
        fd = mkstemp(template);
        if (!VALID_FD(fd))
        {
            LOG(ERR, "Error creating temporary file: %s", strerror(errno));
        }
        else
        {
            fd_printf(fd, "pid:%d\n", pid);
            fd_printf(fd, "pss:%d", pss);
            close(fd);
        }
    }

}


static bool sig_crash_report_mem_max_exists(pid_t pid, int32_t *pss)
{
    DIR *dir;
    FILE *fp;
    struct dirent *entry;
    pid_t pid_tmp;
    char filepath[PATH_MAX];
    int scanned;
    bool found = false;

    dir = opendir(CONFIG_DM_CRASH_REPORTS_TMP_DIR);
    if (dir == NULL)
    {
        LOG(ERR, "Error opening crash report dir: %s", strerror(errno));
        return false;
    }

    while ((entry = readdir(dir)) != NULL)
    {
        if (!str_startswith(entry->d_name, FILENAME_MEM_MAX_PREFIX)) continue;

        snprintf(filepath, sizeof(filepath), "%s/%s", CONFIG_DM_CRASH_REPORTS_TMP_DIR, entry->d_name);
        fp = fopen (filepath , "r");

        if (fp == NULL)
        {
            LOG(ERR, "Error opening temporary file: %s", strerror(errno));
            continue;
        }

        scanned = fscanf(fp, "pid:%d\npss:%d", &pid_tmp, pss);
        fclose(fp);

        if (scanned != 2) {
            LOG(ERR, "Error parsing temporary file: %s\n", strerror(errno));
            continue;
        }

        if (pid == pid_tmp) {
            found = true;
            unlink(filepath);
            break;
        }
    }
    closedir(dir);

    return found;
}


void sig_crash_report(int signum)
{
    char template[128] = CONFIG_DM_CRASH_REPORTS_TMP_DIR "/crashed_XXXXXX";
    char pname[64] = NOT_AVAILABLE_STR;
    int32_t pid;
    int32_t pss;
    bool mem_max_exists = false;

    if (kconfig_enabled(CONFIG_DM_OSYNC_CRASH_REPORTS) && signum != SIGUSR2)
    {
        mkdir(CONFIG_DM_CRASH_REPORTS_TMP_DIR, 0755);

        fd_crash_report = mkstemp(template);
        if (!VALID_FD(fd_crash_report))
        {
            LOG(ERR, "Error creating temporary file: %s", strerror(errno));
        }
        else
        {
            pid = getpid();
            mem_max_exists = sig_crash_report_mem_max_exists(pid, &pss);
            os_pid_to_name(pid, pname, sizeof(pname));

            fd_printf(fd_crash_report, "pid %d\n", pid);
            fd_printf(fd_crash_report, "name %s\n", pname);
            if (mem_max_exists)
            {
                fd_printf(fd_crash_report, "reason Maximum memory limit exceeded (%d kB)\n", pss);
            }
            else
            {
                fd_printf(fd_crash_report, "reason SIG %d (%s)\n",
                          signum, strsignal(signum));
            }
            fd_printf(fd_crash_report, "timestamp %lld\n", (long long)clock_real_ms());
            fd_printf(fd_crash_report, "backtrace ");
        }
    }

    os_backtrace_dump_generic(target_get_btrace_type());

    if (VALID_FD(fd_crash_report))
    {
        fd_printf(fd_crash_report, "\n");
        close(fd_crash_report);
        fd_crash_report = -1;
    }
}

void os_backtrace_dump()
{
    btrace_type btrace = target_get_btrace_type();
    os_backtrace_dump_generic(btrace);
}

bool os_backtrace_dump_cbk(void *ctx, void *addr, const char *func, void *faddr, const char *obj)
{
    struct os_backtrace_dump_info *di = ctx;
    char addr_str[64];
    char line_buf[128];
    int addr_len = 2 + sizeof(void*) * 2;
    int offset = faddr ? addr-faddr : 0;

    snprintf(line_buf, sizeof(line_buf), "%3d > %*p: %26s %#-6x %s", di->frame_no++, addr_len, addr, func, offset, obj);
    crash_print("%s", line_buf);
    crash_report_print(line_buf);

    snprintf(addr_str, sizeof(addr_str), "%p ", addr);
    strlcat(di->addr_info, addr_str, sizeof(di->addr_info));

    return true;
}

bool _os_backtrace_call_func(os_backtrace_func_t *cb_func, void *ctx, void *addr)
{
    Dl_info     dli = { 0 };
    void       *saddr = NULL;
    const char *sname = NULL;
    const char *object = NULL;

    /* No handler, return immediately */
    if (cb_func == NULL) return false;
    if (addr == NULL) return false;

    /* Use dladdr to convert the address to meaningful strings */
    if (dladdr(addr, &dli) != 0)
    {
        sname  = dli.dli_sname;
        saddr  = dli.dli_saddr;
        object = dli.dli_fname;
    }

    /* Call the callback */
    return cb_func(ctx, addr, sname, saddr, object);
}

/**
 * Start backtrace traversal
 */

_Unwind_Reason_Code os_backtrace_handle(struct _Unwind_Context *uc, void *ctx);

struct os_backtrace_func_args
{
    void                   *ctx;
    os_backtrace_func_t  *handler;
    int                 iterations_left;
};

bool os_backtrace(os_backtrace_func_t *cb_func, void *ctx)
{
    struct os_backtrace_func_args args;

    args.handler = cb_func;
    args.ctx     = ctx;
    args.iterations_left = BACKTRACE_MAX_ITERATIONS;

    _Unwind_Backtrace(os_backtrace_handle, &args);

    return true;
}

/**
 * Backtrace handler; this is called by unwind_backtrace() for each stack frame
 *
 * It extracts the stack frame address, the function and objects, and calls the user callback
 */
_Unwind_Reason_Code os_backtrace_handle(struct _Unwind_Context *uc, void *ctx)
{
    struct os_backtrace_func_args      *args = ctx;
    void                               *addr = NULL;

    /* No handler, return immediately */
    if (args->handler == NULL) return _URC_END_OF_STACK;
    if (args->iterations_left <= 0) return _URC_END_OF_STACK;
    args->iterations_left--;

    /* Extract the frame address */
    addr = (void*) _Unwind_GetIP(uc);
    if (addr == NULL)
    {
        /* End of stack, return */
        return _URC_END_OF_STACK;
    }

    /* Call the callback */
    if (!_os_backtrace_call_func(args->handler, args->ctx, addr))
    {
        /* If the handler returned false, stop traversing the stack */
        return _URC_END_OF_STACK;
    }

    return _URC_NO_REASON;
}

// backtrace copy

typedef struct
{
    void **addr;
    int size;
    int count;
    int all;
    bool count_all;
} bt_copy_ctx_t;

static _Unwind_Reason_Code bt_copy_cb(struct _Unwind_Context *uc, void *ctx)
{
    bt_copy_ctx_t *cc = ctx;
    void *addr;

    // Extract the frame address
    addr = (void*) _Unwind_GetIP(uc);
    if (addr == NULL) {
        // End of stack, return
        return _URC_END_OF_STACK;
    }

    cc->all++;

    if (cc->addr && cc->count < cc->size) {
        cc->addr[cc->count] = addr;
        cc->count++;
    } else if (!cc->count_all) {
        return _URC_END_OF_STACK;
    }

    return _URC_NO_REASON;
}

// copy backtrace to addr array
// size : addr size
// count : actual copied (can be null if not needed)
// all : full backtrace length (can be null if not needed)
bool os_backtrace_copy(void **addr, int size, int *count, int *all)
{
    bt_copy_ctx_t cc;
    cc.addr = addr;
    cc.size = size;
    cc.count = 0;
    cc.all = 0;
    cc.count_all = (all != NULL);
    _Unwind_Backtrace(bt_copy_cb, &cc);
    if (count) *count = cc.count;
    if (all) *all = cc.all;
    return true;
}

bool os_backtrace_resolve(void *addr, const char **func, const char **fname)
{
    Dl_info dli;
    // Use dladdr to convert the address to meaningful strings
    if (dladdr(addr, &dli) != 0)
    {
        *func  = dli.dli_sname;
        *fname = dli.dli_fname;
        return true;
    }
    *func  = NULL;
    *fname = NULL;
    return false;
}

void os_backtrace_dump_manager_restart(const char *calling_func)
{
    char template[128] = CONFIG_DM_CRASH_REPORTS_TMP_DIR "/crashed_XXXXXX";

    if (kconfig_enabled(CONFIG_DM_OSYNC_CRASH_REPORTS))
    {
        LOGI("Creating OpenSync restart crash file!");
        mkdir(CONFIG_DM_CRASH_REPORTS_TMP_DIR, 0755);

        fd_crash_report = mkstemp(template);
        if (!VALID_FD(fd_crash_report))
        {
            LOG(ERR, "Error creating temporary file: %s", strerror(errno));
        }
        else
        {
            fd_printf(fd_crash_report, "pid %s\n", NOT_AVAILABLE_STR);
            fd_printf(fd_crash_report, "name OpenSync\n");
            fd_printf(fd_crash_report, "reason OpenSync restarted by %s\n", calling_func);
            fd_printf(fd_crash_report, "timestamp %lld\n", (long long)clock_real_ms());
            fd_printf(fd_crash_report, "backtrace ");
        }
    }

    /* This dumps the backtrace in logs and writes it into the crash file */
    os_backtrace_dump();

    if (VALID_FD(fd_crash_report))
    {
        fd_printf(fd_crash_report, "\n");
        close(fd_crash_report);
        fd_crash_report = -1;
    }
}

#else

void os_backtrace_init(void)
{
    return;
}

void os_backtrace_dump(void)
{
    return;
}

bool os_backtrace(os_backtrace_func_t *func, void *ctx)
{
    return false;
}

void os_backtrace_dump_manager_restart(const char *calling_func)
{
    return;
}

#endif /* WITH_LIBGCC_BACKTRACE */
