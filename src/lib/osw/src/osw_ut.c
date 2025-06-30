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

#include <assert.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <unistd.h>

#include <log.h>
#include <util.h>
#include <module.h>
#include <const.h>
#include <osw_time.h>
#include <osw_timer.h>
#include <osw_drv_common.h>

#include "osw_ut.h"
#include "osw_drv_i.h"

struct osw_ut_module {
    char *name;
    char *file_name;
    char *fun_name;
    void *data;
    char *output;
    bool tested;
    bool passed;
    osw_ut_module_run_f fun;

    struct osw_ut_module *next;
};

static struct osw_ut_module* g_modules = NULL;

void
osw_ut_register_raw(const char *name,
                    const char *file_name,
                    const char *fun_name,
                    osw_ut_module_run_f fun,
                    void *data)
{
    struct osw_ut_module *module;

    assert(name);
    assert(file_name);
    assert(fun_name);
    assert(fun);

    assert((module = calloc(1, sizeof(struct osw_ut_module))));
    assert((module->name = strdup(name)));
    assert((module->file_name = strdup(file_name)));
    assert((module->fun_name = strdup(fun_name)));
    module->fun = fun;
    module->data = data;

    if(g_modules)
        module->next = g_modules;

    g_modules = module;
}

struct osw_ut_proc {
    struct ev_child child;
    struct ev_io io;
    int fds[2];
    char *output;
    bool verbose;
};

static void
osw_ut_child_cb(EV_P_ ev_child *arg, int events)
{
    ev_child_stop(EV_A_ arg);
}

static void
osw_ut_io_cb(EV_P_ ev_io *arg, int events)
{
    struct osw_ut_proc *proc = container_of(arg, struct osw_ut_proc, io);
    char buf[4096] = {0};
    ssize_t len = read(arg->fd, buf, sizeof(buf) - 1);

    if (len <= 0) {
        ev_io_stop(EV_A_ arg);
        close(arg->fd);
        return;
    }

    /* FIXME: This could be more efficient (less copying)
     * and also rotate logs (to avoid running out of memory
     * if logs become excessively long (which shouldn't
     * really happen, but hey). This is good enough for now.
     */
    if (proc->verbose)
        WARN_ON(write(fileno(stdout), buf, len) != len);
    else
        strgrow(&proc->output, "%s", buf);
}

static void
osw_ut_run_proc(struct osw_ut_proc *p, struct osw_ut_module *m)
{
    assert(pipe(p->fds) == 0);
    const pid_t pid = fork();

    if (pid == 0) {
        assert(close(p->fds[0]) == 0);
        assert(close(1) == 0);
        assert(close(2) == 0);
        assert(dup2(p->fds[1], 1) == 1);
        assert(dup2(p->fds[1], 2) == 2);
        ev_loop_destroy(EV_DEFAULT);
        ev_default_loop(EVFLAG_FORKCHECK);
        osw_drv_unregister_all();
        m->fun(m->data);
        exit(EXIT_SUCCESS);
        return;
    }

    assert(close(p->fds[1]) == 0);
    ev_child_init(&p->child, osw_ut_child_cb, pid, 0);
    ev_child_start(EV_DEFAULT_ &p->child);
    ev_io_init(&p->io, osw_ut_io_cb, p->fds[0], EV_READ);
    ev_io_start(EV_DEFAULT_ &p->io);
    ev_run(EV_DEFAULT_ 0);
    ev_child_stop(EV_DEFAULT_ &p->child);
}

static void
osw_ut_run_one(struct osw_ut_module *m,
               bool dont_fork,
               bool verbose)
{
    m->tested = true;
    if (dont_fork == true) {
        m->passed = false;
        fprintf(stderr, "    RUN: %s: %s\n", m->file_name, m->fun_name);
        m->fun(m->data);
        fprintf(stderr, "   PASS: %s: %s\n", m->file_name, m->fun_name);
        m->passed = true;
    }
    else {
        struct osw_ut_proc proc = {0};

        proc.verbose = verbose;
        if (verbose)
            fprintf(stderr, "    RUN: %s: %s\n", m->file_name, m->fun_name);

        osw_ut_run_proc(&proc, m);

        if (WIFEXITED(proc.child.rstatus)) {
            if (verbose)
                fprintf(stderr, "   PASS: %s: %s\n", m->file_name, m->fun_name);
            m->passed = true;
            free(proc.output);
        } else {
            if (verbose)
                fprintf(stderr, "!!!FAIL: %s: %s (logs below)\n", m->file_name, m->fun_name);
            m->passed = false;
            m->output = proc.output;
        }
    }
}

bool
osw_ut_run_by_prefix(const char *prefix,
                     bool dont_fork,
                     bool verbose)
{
    struct osw_ut_module *m;
    for (m = g_modules; m != NULL; m = m->next)
        if (prefix == NULL || strstr(m->fun_name, prefix) == m->fun_name)
            osw_ut_run_one(m, dont_fork, verbose);

    int tested = 0;
    int passed = 0;
    for (m = g_modules; m != NULL; m = m->next) {
        if (prefix == NULL || strstr(m->fun_name, prefix) == m->fun_name) {
            if (m->tested) {
                tested++;
            }

            if (m->passed == true) {
               passed++;
            }
            else if (m->passed == false) {
                fprintf(stderr, "LOGS: %s: %s:\n%s\n", m->file_name, m->fun_name, m->output ?: "");
                free(m->output);
            }
        }
    }

    fprintf(stderr, "\n\n");

    for (m = g_modules; m != NULL; m = m->next) {
        if (prefix == NULL || strstr(m->fun_name, prefix) == m->fun_name) {
            if (m->passed == false) {
                fprintf(stderr, "FAIL: %s: %s (logs above)\n", m->file_name, m->fun_name);
            }
        }
    }

    fprintf(stderr, "UT: passed %d/%d, failed %d\n", passed, tested, (tested - passed));

    return (passed == tested);
}

bool
osw_ut_run_all(bool dont_fork,
               bool verbose)
{
    return osw_ut_run_by_prefix(NULL, dont_fork, verbose);
}

void
osw_ut_print_test_names(void)
{
    unsigned int cnt = 0;
    struct osw_ut_module *m;

    printf("Test cases:\n");
    for (m = g_modules; m != NULL; m = m->next) {
        printf("    %s\n", m->fun_name);
        cnt++;
    }
    printf("Number of test cases: %u\n", cnt);
}


void
osw_ut_time_init(void)
{
    osw_time_set_mono_clk(0);
    osw_time_set_wall_clk(0);
}

void
osw_ut_time_advance(uint64_t delta_nsec)
{
    const uint64_t new_now_nsec = osw_time_mono_clk() + delta_nsec;
    bool keep_running;

    while (true) {
        uint64_t next_at_nsec;

        osw_timer_core_dispatch(osw_time_mono_clk());
        keep_running = osw_timer_core_get_next_at(&next_at_nsec);
        if (keep_running == false)
            break;

        if (next_at_nsec > new_now_nsec)
            break;

        osw_time_set_mono_clk(next_at_nsec);
        osw_time_set_wall_clk(next_at_nsec);
    }

    osw_time_set_mono_clk(new_now_nsec);
    osw_time_set_wall_clk(new_now_nsec);

    WARN_ON(osw_drv_poll() == false);
}
