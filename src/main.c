#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <ncurses.h>
#include "fleet.h"
#include "collector.h"
#include "ui.h"

FleetCluster g_fleet;

static void handle_signal(int sig) {
    (void)sig;
    g_fleet.running = false;
}

void fleet_init(FleetCluster *fleet, double refresh_rate) {
    fleet->running = true;
    fleet->refresh_rate_sec = refresh_rate;

    char current_host[64] = {0};
    gethostname(current_host, sizeof(current_host) - 1);

    /* Node 0: bazina (Left) */
    strncpy(fleet->nodes[0].name, "bazina", sizeof(fleet->nodes[0].name) - 1);
    strncpy(fleet->nodes[0].ip, "10.0.0.15", sizeof(fleet->nodes[0].ip) - 1);
    strncpy(fleet->nodes[0].role, "Debian 13 (Navi 10 GPU, ASUS HDMI)", sizeof(fleet->nodes[0].role) - 1);
    fleet->nodes[0].is_local = (strcasecmp(current_host, "bazina") == 0);
    fleet->nodes[0].is_online = false;
    fleet->nodes[0].num_cores = 4;
    pthread_mutex_init(&fleet->nodes[0].lock, NULL);

    /* Node 1: t520 (Center / Master) */
    strncpy(fleet->nodes[1].name, "t520", sizeof(fleet->nodes[1].name) - 1);
    strncpy(fleet->nodes[1].ip, "10.0.0.18", sizeof(fleet->nodes[1].ip) - 1);
    strncpy(fleet->nodes[1].role, "Manjaro (KVM Server, NTP Master)", sizeof(fleet->nodes[1].role) - 1);
    fleet->nodes[1].is_local = (strcasecmp(current_host, "t520") == 0);
    fleet->nodes[1].is_online = false;
    fleet->nodes[1].num_cores = 8;
    pthread_mutex_init(&fleet->nodes[1].lock, NULL);

    /* Node 2: boxC (Right / Storage) */
    strncpy(fleet->nodes[2].name, "boxC", sizeof(fleet->nodes[2].name) - 1);
    strncpy(fleet->nodes[2].ip, "10.0.0.14", sizeof(fleet->nodes[2].ip) - 1);
    strncpy(fleet->nodes[2].role, "Pi OS (916GB kof17 NFS/Samba)", sizeof(fleet->nodes[2].role) - 1);
    fleet->nodes[2].is_local = (strcasecmp(current_host, "boxC") == 0);
    fleet->nodes[2].is_online = false;
    fleet->nodes[2].num_cores = 4;
    pthread_mutex_init(&fleet->nodes[2].lock, NULL);
}

void fleet_destroy(FleetCluster *fleet) {
    for (int i = 0; i < MAX_NODES; i++) {
        pthread_mutex_destroy(&fleet->nodes[i].lock);
    }
}

static void print_usage(const char *prog) {
    printf("⚡ fleetMonitor — High-Performance ncurses Fleet Monitor in C\n");
    printf("Topology: [bazina (10.0.0.15)] <---> [t520 (10.0.0.18)] <---> [boxC (10.0.0.14)]\n\n");
    printf("Usage: %s [OPTIONS]\n", prog);
    printf("Options:\n");
    printf("  -r <sec>    Set refresh interval in seconds (default: 1.0)\n");
    printf("  -h, --help  Show this help message\n");
    printf("  -v          Show version information\n");
}

int main(int argc, char *argv[]) {
    double refresh_rate = 1.0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-r") == 0 && i + 1 < argc) {
            refresh_rate = atof(argv[++i]);
            if (refresh_rate < 0.2) refresh_rate = 0.2;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            printf("fleetMonitor version 1.0.0 (16 cores)\n");
            return 0;
        }
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    fleet_init(&g_fleet, refresh_rate);
    collector_start(&g_fleet);
    ui_init();
    timeout(100); /* 100ms non-busy input poll (10 FPS smooth updates) */

    while (g_fleet.running) {
        int ch = getch();
        if (ch == 'q' || ch == 'Q' || ch == 27) { /* 27 is ESC */
            g_fleet.running = false;
            break;
        }

        ui_render(&g_fleet);
    }

    ui_destroy();
    collector_stop(&g_fleet);
    fleet_destroy(&g_fleet);

    printf("fleetMonitor exited cleanly.\n");
    return 0;
}
