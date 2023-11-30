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

#if defined(WITH_LIBGCC_BACKTRACE)
#include <unwind.h>
#endif

#include "os_common.h"
#include "os_time.h"
#include "os_proc.h"
#include "osp_unit.h"

#include "log.h"
#include "os.h"
#include "os_backtrace.h"
#include "os_file_ops.h"
#include "target.h"

#if defined(WITH_LIBGCC_BACKTRACE)

#define MODULE_ID  LOG_MODULE_ID_COMMON

#define BACKTRACE_MAX_ITERATIONS 20

/**
 * Stack-trace functions, mostly inspired from libubacktrace
 */
void                            os_backtrace_sig_crash(int signum);
static os_backtrace_func_t      os_backtrace_dump_cbk;

static FILE                     *fp = NULL;
static FILE                     *fp_crash_report = NULL;

/**
 * Install crash handlers that dump the current stack in the log file
 */
void os_backtrace_init(void)
{
    struct sigaction sa;

    sigemptyset(&sa.sa_mask);
    sa.sa_handler   = os_backtrace_sig_crash;
    sa.sa_flags     = SA_RESETHAND;

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
 * The crash handler
 */
void os_backtrace_sig_crash(int signum)
{
    LOG(ALERT, "Signal %d received, generating stack dump...\n", signum);

    sig_crash_report(signum);

    if (signum != SIGUSR2)
    {
        /* At this point the handler for this signal was reset (except for SIGUSR2) due to the SA_RESETHAND flag;
           so re-send the signal to ourselves in order to properly crash */
        raise(signum);
    }
}

/**
 * Log current stack trace
 */
struct os_backtrace_dump_info
{
    int frame_no;
    char addr_info[512];
};

static void crash_print(char *fmt, ...)
{
    char    buf[BFR_SIZE_512];
    va_list args;

    memset(buf, 0x00, sizeof(buf));
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);

    if (NULL != fp)
    {
        fprintf(fp, "%s\n", buf);
    }

    LOG(ERR, "%s", buf);
    va_end(args);
}

static void crash_report_print(char *line)
{
    if (fp_crash_report)
    {
        fprintf(fp_crash_report, "%s\\n", line);
    }
}

void os_backtrace_dump_generic(btrace_type btrace)
{
    struct os_backtrace_dump_info di;

    if (btrace != BTRACE_LOG_ONLY) {
        /* This call opens up a file </location/crashed_<process_name>_<timestamp>.pid> */
        fp = os_file_open(BTRACE_DUMP_PATH, "crashed");
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

    if (btrace != BTRACE_LOG_ONLY) {
        os_file_close(fp);
    }
}

void sig_crash_report(int signum)
{
    char template[128] = CRASH_REPORTS_TMP_DIR "/crashed_XXXXXX";
    char pname[64] = "<NA>";
    int32_t pid;

    if (kconfig_enabled(CONFIG_DM_OSYNC_CRASH_REPORTS) && signum != SIGUSR2)
    {
        int fd;

        mkdir(CRASH_REPORTS_TMP_DIR, 0755);

        fp_crash_report = NULL;
        if ((fd = mkstemp(template)) != -1)
        {
            fp_crash_report = fdopen(fd, "w");
            if (fp_crash_report == NULL) close(fd);
        }
        if (fp_crash_report == NULL)
        {
            LOG(ERR, "Error creating temporary file: %s", strerror(errno));
        }

        if (fp_crash_report != NULL)
        {
            pid = getpid();
            os_pid_to_name(pid, pname, sizeof(pname));

            fprintf(fp_crash_report, "pid %d\n", pid);
            fprintf(fp_crash_report, "name %s\n", pname);
            fprintf(fp_crash_report, "reason SIG %d (%s)\n",
                    signum, strsignal(signum));
            fprintf(fp_crash_report, "timestamp %lld\n", (long long)clock_real_ms());
            fprintf(fp_crash_report, "backtrace ");
        }
    }

    os_backtrace_dump_generic(target_get_btrace_type());
    if (fp_crash_report)
    {
        fprintf(fp_crash_report, "\n");

        fclose(fp_crash_report);
        fp_crash_report = NULL;
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

#endif /* WITH_LIBGCC_BACKTRACE */
