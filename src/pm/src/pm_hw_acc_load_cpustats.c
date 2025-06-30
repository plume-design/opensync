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

#include "pm_hw_acc_load_cpustats.h"

#include <inttypes.h>
#include <sys/sysinfo.h>
#include <string.h>

#include <memutil.h>
#include <util.h>
#include "log.h"

/* These numbers identify the amount of time the CPU has spent performing different kinds of work
 * Time units are in USER_HZ or Jiffies (typically hundredths of a second).
 */
struct cpustats
{
    uint64_t user_hz;
    uint64_t nice_hz;
    uint64_t system_hz;
    uint64_t idle_hz;
    uint64_t iowait_hz;
    uint64_t steal_hz;
    uint64_t irq_hz;
    uint64_t softirq_hz;
    uint64_t guest_hz;
    uint64_t guest_nice_hz;
};

struct pm_hw_acc_load_cpustats
{
    struct cpustats *stats;
    size_t stats_len;
};

/* Processed data expressed in percentages */
struct cpuload
{
    uint8_t softirq;
    uint8_t idle;
    // XXX: add more stats in the future if needed
};

struct pm_hw_acc_load_cpuload
{
    struct cpuload *load;
    size_t load_len;
};

/* Faster than strstr() */
static bool starts_with_cpu(const char *str)
{
    return ((str[0] - 'c') | (str[1] - 'p') | (str[2] - 'u')) == 0;
}

/*
 * Output of /proc/stat
    cpu  600963 1234 231797 13054284 80265 0 7601 0 0 0
    cpu0 62895 166 30429 1632944 8316 0 2151 0 0 0
    cpu1 74688 289 31835 1626514 14780 0 453 0 0 0
    cpu2 69893 117 28074 1633759 6481 0 4465 0 0 0
    cpu3 75135 77 28506 1632899 12822 0 149 0 0 0
*/
static struct pm_hw_acc_load_cpustats *pm_hw_acc_load_cpustats_parse(char *lines, const size_t soft_cpu_limit)
{
    if (lines == NULL) return NULL;

    struct pm_hw_acc_load_cpustats *cpu = CALLOC(1, sizeof(*cpu));
    cpu->stats = CALLOC(soft_cpu_limit, sizeof(*cpu->stats));
    int cpu_number = -1;
    char *line;

    while ((line = strsep(&lines, "\n")) != NULL)
    {
        if (!starts_with_cpu(line)) continue;
        if (line[3] == ' ') continue;
        if (sscanf(line + 3, "%d ", &cpu_number) != 1) continue;

        /* If we read higher CPU number than current upper soft-limit (soft_cpu_limit)
         * we need to prepare more space for data and memzero in case there's a hole.
         */
        if ((size_t)cpu_number >= soft_cpu_limit)
        {
            const size_t diff = (cpu_number + 1) - soft_cpu_limit;
            cpu->stats = REALLOC(cpu->stats, (cpu_number + 1) * sizeof(*cpu->stats));
            memset(&cpu->stats[soft_cpu_limit], 0, diff * sizeof(*cpu->stats));
        }

        struct cpustats *pcs = &cpu->stats[cpu_number];
        sscanf(line,
               "%*s"
               " %" PRIu64 " %" PRIu64 " %" PRIu64 ""
               " %" PRIu64 " %" PRIu64 " %" PRIu64 ""
               " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 "",
               &pcs->user_hz,
               &pcs->nice_hz,
               &pcs->system_hz,
               &pcs->idle_hz,
               &pcs->iowait_hz,
               &pcs->irq_hz,
               &pcs->softirq_hz,
               &pcs->steal_hz,
               &pcs->guest_hz,
               &pcs->guest_nice_hz);
    }
    cpu->stats_len = cpu_number + 1;
    return cpu;
}

struct pm_hw_acc_load_cpustats *pm_hw_acc_load_cpustats_get_from_str(char *lines, const size_t cpu_init)
{
    return pm_hw_acc_load_cpustats_parse(lines, cpu_init);
}

struct pm_hw_acc_load_cpustats *pm_hw_acc_load_cpustats_get(const size_t cpu_init)
{
    return pm_hw_acc_load_cpustats_get_from_str(file_geta("/proc/stat"), cpu_init);
}

void pm_hw_acc_load_cpustats_drop(struct pm_hw_acc_load_cpustats *cpu)
{
    if (cpu == NULL) return;
    FREE(cpu->stats);
    FREE(cpu);
}

static uint8_t pm_hw_acc_load_cpustats_percent_value(const uint64_t prev, const uint64_t next, const uint64_t itv)
{
    const uint64_t ret = ((next - prev) * 100) / itv;
    return (ret > 100) ? 100 : ret;
}

static uint64_t pm_hw_acc_load_cpustats_get_uptime(const struct cpustats *stats)
{
    return stats->user_hz + stats->nice_hz + stats->system_hz + stats->idle_hz + stats->iowait_hz + stats->irq_hz
           + stats->softirq_hz + stats->steal_hz;
}

void pm_hw_acc_load_cpustats_compare(
        const struct pm_hw_acc_load_cpustats *prev_stats,
        const struct pm_hw_acc_load_cpustats *next_stats,
        struct pm_hw_acc_load_cpuload *cpu)
{
    size_t i;
    if (WARN_ON(cpu->load_len != prev_stats->stats_len) || WARN_ON(prev_stats->stats_len != next_stats->stats_len))
        return;

    for (i = 0; i < cpu->load_len; i++)
    {
        const uint64_t p_uptime = pm_hw_acc_load_cpustats_get_uptime(&prev_stats->stats[i]);
        const uint64_t n_uptime = pm_hw_acc_load_cpustats_get_uptime(&next_stats->stats[i]);
        const uint64_t itv = n_uptime - p_uptime;

        /* If there's no interval, CPU was completely idle or offline */
        if (itv == 0 || n_uptime == 0 || p_uptime == 0)
        {
            cpu->load[i].idle = 100;
        }
        else
        {
            cpu->load[i].idle = pm_hw_acc_load_cpustats_percent_value(
                    prev_stats->stats[i].idle_hz,
                    next_stats->stats[i].idle_hz,
                    itv);
        }
        LOGD("%s: cpu[%zu] idle: %" PRIu8 "%%", __func__, i, cpu->load[i].idle);
    }
}

static size_t pm_hw_acc_load_cpustats_get_cpu_count(void)
{
    size_t proc_nr = 0;
    char *line;
    char *file = file_geta("/proc/stat");
    if (file == NULL) return 0;

    while ((line = strsep(&file, "\n")) != NULL)
    {
        if (!starts_with_cpu(line)) continue;
        if (line[3] != ' ')
        {
            size_t num;
            if (sscanf(line + 3, "%zu", &num) == 1 && num > proc_nr)
            {
                proc_nr = num;
            }
        }
    }
    return proc_nr + 1;
}

size_t pm_hw_acc_load_cpustats_get_len(struct pm_hw_acc_load_cpustats *cpu)
{
    return cpu->stats_len;
}

size_t pm_hw_acc_load_cpuload_get_len(struct pm_hw_acc_load_cpuload *cpu)
{
    return cpu->load_len;
}

bool pm_hw_acc_load_cpustats_need_more_space(
        const struct pm_hw_acc_load_cpustats *prev,
        const struct pm_hw_acc_load_cpustats *next)
{
    return (next->stats_len > prev->stats_len) ? true : false;
}

unsigned pm_hw_acc_load_compute_max_cpu_util(const struct pm_hw_acc_load_cpuload *cpu)
{
    size_t i;
    unsigned max = 0;
    for (i = 0; i < cpu->load_len; i++)
    {
        const unsigned cpu_util = 100 - cpu->load[i].idle;
        if (cpu_util > max)
        {
            max = cpu_util;
        }
    }
    return max;
}

struct pm_hw_acc_load_cpuload *pm_hw_acc_load_cpuload_alloc(void)
{
    struct pm_hw_acc_load_cpuload *cpu = CALLOC(1, sizeof(*cpu));
    cpu->load_len = pm_hw_acc_load_cpustats_get_cpu_count();
    cpu->load = CALLOC(cpu->load_len, sizeof(*cpu->load));
    return cpu;
}

void pm_hw_acc_load_cpuload_drop(struct pm_hw_acc_load_cpuload *cpu)
{
    if (cpu == NULL) return;
    FREE(cpu->load);
    FREE(cpu);
}

void pm_hw_acc_load_cpuload_extend(struct pm_hw_acc_load_cpuload **cpu, const size_t new_len)
{
    LOGI("%s: resizing cpu load size from '%zu' to '%zu'", __func__, (*cpu)->load_len, new_len);
    (*cpu)->load = REALLOC((*cpu)->load, new_len * sizeof(*(*cpu)->load));
    (*cpu)->load_len = new_len;
}
