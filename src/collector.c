#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include "collector.h"

static double calculate_pct(CpuTicks current, CpuTicks *prev) {
    if (prev->total == 0) {
        *prev = current;
        return 0.0;
    }
    int64_t d_total = (int64_t)current.total - (int64_t)prev->total;
    int64_t d_idle = (int64_t)current.idle - (int64_t)prev->idle;
    *prev = current;

    if (d_total <= 0) return 0.0;
    int64_t d_busy = d_total - d_idle;
    if (d_busy < 0) d_busy = 0;
    double pct = (100.0 * (double)d_busy) / (double)d_total;
    if (pct < 0.0) pct = 0.0;
    if (pct > 100.0) pct = 100.0;
    return pct;
}

static void *node_collector_thread(void *arg) {
    FleetNode *node = (FleetNode *)arg;

    char cmd[2048];
    if (node->is_local) {
        snprintf(cmd, sizeof(cmd),
            "awk '/^cpu[0-9]/ { print \"CORE\", $1, $2, $3, $4, $5, $6, $7, $8 } "
            "/^cpu / { print \"CPUTOT\", $2, $3, $4, $5, $6, $7, $8 } "
            "/^MemTotal:/ { print \"MEMTOTAL\", $2 } "
            "/^MemAvailable:/ { print \"MEMAVAIL\", $2 } "
            "/^SwapTotal:/ { print \"SWAPTOTAL\", $2 } "
            "/^SwapFree:/ { print \"SWAPFREE\", $2 }' /proc/stat /proc/meminfo; "
            "awk '{ print \"LOAD\", $1, $2, $3 }' /proc/loadavg; "
            "df -k / /mnt/data /mnt/kof17 2>/dev/null | awk 'NR>1 { print \"DISK\", $6, $2, $3, $5 }'");
    } else {
        snprintf(cmd, sizeof(cmd),
            "ssh -o BatchMode=yes -o ConnectTimeout=2 -o ServerAliveInterval=2 -o ServerAliveCountMax=2 %s "
            "\"awk '/^cpu[0-9]/ { print \\\"CORE\\\", \\$1, \\$2, \\$3, \\$4, \\$5, \\$6, \\$7, \\$8 } "
            "/^cpu / { print \\\"CPUTOT\\\", \\$2, \\$3, \\$4, \\$5, \\$6, \\$7, \\$8 } "
            "/^MemTotal:/ { print \\\"MEMTOTAL\\\", \\$2 } "
            "/^MemAvailable:/ { print \\\"MEMAVAIL\\\", \\$2 } "
            "/^SwapTotal:/ { print \\\"SWAPTOTAL\\\", \\$2 } "
            "/^SwapFree:/ { print \\\"SWAPFREE\\\", \\$2 }' /proc/stat /proc/meminfo; "
            "awk '{ print \\\"LOAD\\\", \\$1, \\$2, \\$3 }' /proc/loadavg; "
            "df -k / /mnt/data /mnt/kof17 2>/dev/null | awk 'NR>1 { print \\\"DISK\\\", \\$6, \\$2, \\$3, \\$5 }'\"",
            node->name);
    }

    while (g_fleet.running) {
        struct timespec t_start, t_end;
        clock_gettime(CLOCK_MONOTONIC, &t_start);

        FILE *fp = popen(cmd, "r");
        if (!fp) {
            pthread_mutex_lock(&node->lock);
            node->is_online = false;
            pthread_mutex_unlock(&node->lock);
        } else {
            char line[512];
            bool got_data = false;

            /* Temporary structures before acquiring lock */
            CpuTicks tmp_total_ticks = {0, 0};
            CpuTicks tmp_core_ticks[MAX_CORES];
            bool has_core[MAX_CORES];
            int found_cores = 0;
            memset(tmp_core_ticks, 0, sizeof(tmp_core_ticks));
            memset(has_core, 0, sizeof(has_core));

            uint64_t mem_tot = 0, mem_avail = 0, swap_tot = 0, swap_free = 0;
            double l1 = 0.0, l5 = 0.0, l15 = 0.0;
            DiskInfo disks_tmp[MAX_DISKS];
            int num_disks_tmp = 0;

            while (fgets(line, sizeof(line), fp)) {
                got_data = true;
                if (strncmp(line, "CPUTOT", 6) == 0) {
                    uint64_t u, n, s, id, io, irq, sirq;
                    if (sscanf(line + 7, "%lu %lu %lu %lu %lu %lu %lu",
                               &u, &n, &s, &id, &io, &irq, &sirq) >= 4) {
                        tmp_total_ticks.total = u + n + s + id + io + irq + sirq;
                        tmp_total_ticks.idle = id + io;
                    }
                } else if (strncmp(line, "CORE", 4) == 0) {
                    char cname[16];
                    uint64_t u, n, s, id, io, irq, sirq;
                    if (sscanf(line + 5, "%s %lu %lu %lu %lu %lu %lu %lu",
                               cname, &u, &n, &s, &id, &io, &irq, &sirq) >= 5) {
                        int c_idx = atoi(cname + 3);
                        if (c_idx >= 0 && c_idx < MAX_CORES) {
                            tmp_core_ticks[c_idx].total = u + n + s + id + io + irq + sirq;
                            tmp_core_ticks[c_idx].idle = id + io;
                            has_core[c_idx] = true;
                            if (c_idx + 1 > found_cores) found_cores = c_idx + 1;
                        }
                    }
                } else if (strncmp(line, "MEMTOTAL", 8) == 0) {
                    sscanf(line + 9, "%lu", &mem_tot);
                } else if (strncmp(line, "MEMAVAIL", 8) == 0) {
                    sscanf(line + 9, "%lu", &mem_avail);
                } else if (strncmp(line, "SWAPTOTAL", 9) == 0) {
                    sscanf(line + 10, "%lu", &swap_tot);
                } else if (strncmp(line, "SWAPFREE", 8) == 0) {
                    sscanf(line + 9, "%lu", &swap_free);
                } else if (strncmp(line, "LOAD", 4) == 0) {
                    sscanf(line + 5, "%lf %lf %lf", &l1, &l5, &l15);
                } else if (strncmp(line, "DISK", 4) == 0) {
                    if (num_disks_tmp < MAX_DISKS) {
                        char mnt[32];
                        uint64_t tot_k, used_k;
                        char pct_str[16];
                        if (sscanf(line + 5, "%s %lu %lu %s", mnt, &tot_k, &used_k, pct_str) >= 3) {
                            snprintf(disks_tmp[num_disks_tmp].mount, sizeof(disks_tmp[num_disks_tmp].mount), "%s", mnt);
                            disks_tmp[num_disks_tmp].total_kb = tot_k;
                            disks_tmp[num_disks_tmp].used_kb = used_k;
                            disks_tmp[num_disks_tmp].percent = atoi(pct_str);
                            num_disks_tmp++;
                        }
                    }
                }
            }

            int exit_code = pclose(fp);
            clock_gettime(CLOCK_MONOTONIC, &t_end);
            double rtt_ms = (t_end.tv_sec - t_start.tv_sec) * 1000.0 +
                            (t_end.tv_nsec - t_start.tv_nsec) / 1000000.0;

            pthread_mutex_lock(&node->lock);
            if (exit_code == 0 && got_data) {
                node->is_online = true;
                node->last_seen = time(NULL);
                node->response_ms = rtt_ms;

                if (tmp_total_ticks.total > 0) {
                    node->total_cpu_usage = calculate_pct(tmp_total_ticks, &node->prev_total_ticks);
                }

                if (found_cores > 0) {
                    node->num_cores = found_cores;
                    for (int i = 0; i < found_cores; i++) {
                        if (has_core[i]) {
                            node->core_usage[i] = calculate_pct(tmp_core_ticks[i], &node->prev_core_ticks[i]);
                        }
                    }
                }

                if (mem_tot > 0) {
                    node->mem_total_kb = mem_tot;
                    node->mem_avail_kb = mem_avail;
                    node->mem_used_kb = (mem_tot >= mem_avail) ? (mem_tot - mem_avail) : 0;
                }
                node->swap_total_kb = swap_tot;
                node->swap_free_kb = swap_free;
                node->swap_used_kb = (swap_tot >= swap_free) ? (swap_tot - swap_free) : 0;

                node->load1 = l1;
                node->load5 = l5;
                node->load15 = l15;

                node->num_disks = num_disks_tmp;
                for (int i = 0; i < num_disks_tmp; i++) {
                    node->disks[i] = disks_tmp[i];
                }
            } else {
                node->is_online = false;
            }
            pthread_mutex_unlock(&node->lock);
        }

        /* Sleep in short 100ms intervals for quick shutdown */
        int total_ms = (int)(g_fleet.refresh_rate_sec * 1000.0);
        if (total_ms < 500) total_ms = 500;
        for (int elapsed = 0; elapsed < total_ms && g_fleet.running; elapsed += 100) {
            usleep(100000);
        }
    }

    return NULL;
}

void collector_start(FleetCluster *fleet) {
    for (int i = 0; i < MAX_NODES; i++) {
        pthread_create(&fleet->nodes[i].thread, NULL, node_collector_thread, &fleet->nodes[i]);
    }
}

void collector_stop(FleetCluster *fleet) {
    for (int i = 0; i < MAX_NODES; i++) {
        pthread_join(fleet->nodes[i].thread, NULL);
    }
}
