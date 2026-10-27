#ifndef FLEET_H
#define FLEET_H

#define _GNU_SOURCE
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <pthread.h>

#define MAX_CORES 32
#define MAX_DISKS 4
#define MAX_NODES 3

typedef struct {
    uint64_t total;
    uint64_t idle;
} CpuTicks;

typedef struct {
    char mount[32];
    uint64_t total_kb;
    uint64_t used_kb;
    int percent;
} DiskInfo;

typedef struct {
    char name[32];          /* "bazina", "t520", "boxC" */
    char ip[32];            /* "10.0.0.15", "10.0.0.18", "10.0.0.14" */
    char role[64];          /* Subtitle / role description */
    bool is_local;          /* true if this is t520 */
    bool is_online;         /* true if reachable */
    double response_ms;     /* Roundtrip command latency */
    time_t last_seen;       /* Timestamp of last successful contact */

    /* CPU */
    int num_cores;
    double core_usage[MAX_CORES];
    double total_cpu_usage;
    CpuTicks prev_total_ticks;
    CpuTicks prev_core_ticks[MAX_CORES];

    /* Load Average */
    double load1;
    double load5;
    double load15;

    /* Memory (in KB) */
    uint64_t mem_total_kb;
    uint64_t mem_avail_kb;
    uint64_t mem_used_kb;
    uint64_t swap_total_kb;
    uint64_t swap_free_kb;
    uint64_t swap_used_kb;

    /* Disks */
    int num_disks;
    DiskInfo disks[MAX_DISKS];

    pthread_mutex_t lock;
    pthread_t thread;
} FleetNode;

typedef struct {
    FleetNode nodes[MAX_NODES];
    bool running;
    double refresh_rate_sec;
} FleetCluster;

extern FleetCluster g_fleet;

void fleet_init(FleetCluster *fleet, double refresh_rate);
void fleet_destroy(FleetCluster *fleet);

#endif /* FLEET_H */
