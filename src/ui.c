#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ncurses.h>
#include <locale.h>
#include <time.h>
#include "ui.h"

#define PAIR_DEFAULT   1
#define PAIR_CYAN      2
#define PAIR_GREEN     3
#define PAIR_YELLOW    4
#define PAIR_RED       5
#define PAIR_MAGENTA   6
#define PAIR_BLUE      7
#define PAIR_STATUSBAR 8
#define PAIR_DIM       9

static WINDOW *s_panels[MAX_NODES] = {NULL, NULL, NULL};
static int s_prev_max_y = -1;
static int s_prev_max_x = -1;

static void cleanup_windows(void) {
    for (int i = 0; i < MAX_NODES; i++) {
        if (s_panels[i]) {
            delwin(s_panels[i]);
            s_panels[i] = NULL;
        }
    }
}

void ui_init(void) {
    setlocale(LC_ALL, "");
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);
    curs_set(0);

    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(PAIR_DEFAULT,   COLOR_WHITE,   -1);
        init_pair(PAIR_CYAN,      COLOR_CYAN,    -1);
        init_pair(PAIR_GREEN,     COLOR_GREEN,   -1);
        init_pair(PAIR_YELLOW,    COLOR_YELLOW,  -1);
        init_pair(PAIR_RED,       COLOR_RED,     -1);
        init_pair(PAIR_MAGENTA,   COLOR_MAGENTA, -1);
        init_pair(PAIR_BLUE,      COLOR_BLUE,    -1);
        init_pair(PAIR_STATUSBAR, COLOR_BLACK,   COLOR_CYAN);
        init_pair(PAIR_DIM,       COLOR_WHITE,   -1);
    }
}

void ui_destroy(void) {
    cleanup_windows();
    endwin();
}

static void format_kb(uint64_t kb, char *out, size_t sz) {
    if (kb >= 1024 * 1024) {
        snprintf(out, sz, "%.1fG", (double)kb / (1024.0 * 1024.0));
    } else if (kb >= 1024) {
        snprintf(out, sz, "%.0fM", (double)kb / 1024.0);
    } else {
        snprintf(out, sz, "%luK", kb);
    }
}

static void draw_bar(WINDOW *win, int y, int x, int width, double pct) {
    if (width <= 0) return;
    if (pct < 0.0) pct = 0.0;
    if (pct > 100.0) pct = 100.0;

    int fill_chars = (int)((pct / 100.0) * width);
    if (fill_chars > width) fill_chars = width;

    int color = PAIR_GREEN;
    if (pct >= 85.0) {
        color = PAIR_RED;
    } else if (pct >= 60.0) {
        color = PAIR_YELLOW;
    }

    wattron(win, COLOR_PAIR(color) | A_BOLD);
    for (int i = 0; i < fill_chars; i++) {
        mvwprintw(win, y, x + i, "█");
    }
    wattroff(win, COLOR_PAIR(color) | A_BOLD);

    wattron(win, COLOR_PAIR(PAIR_CYAN) | A_DIM);
    for (int i = fill_chars; i < width; i++) {
        mvwprintw(win, y, x + i, "░");
    }
    wattroff(win, COLOR_PAIR(PAIR_CYAN) | A_DIM);
}

static void render_node_panel(WINDOW *win, FleetNode *node, int h, int w) {
    pthread_mutex_lock(&node->lock);
    FleetNode n = *node;
    pthread_mutex_unlock(&node->lock);

    werase(win);

    /* Border */
    wattron(win, COLOR_PAIR(PAIR_CYAN));
    box(win, 0, 0);
    wattroff(win, COLOR_PAIR(PAIR_CYAN));

    /* Title */
    wattron(win, COLOR_PAIR(PAIR_CYAN) | A_BOLD);
    mvwprintw(win, 0, 2, " [ %s ] ", n.name);
    wattroff(win, COLOR_PAIR(PAIR_CYAN) | A_BOLD);

    /* Online/Offline status & latency */
    int y = 1;
    if (n.is_online) {
        wattron(win, COLOR_PAIR(PAIR_GREEN) | A_BOLD);
        mvwprintw(win, y, 2, "● ONLINE");
        wattroff(win, COLOR_PAIR(PAIR_GREEN) | A_BOLD);
        wattron(win, COLOR_PAIR(PAIR_DEFAULT));
        wprintw(win, "  %s  (%.0f ms)", n.ip, n.response_ms);
        wattroff(win, COLOR_PAIR(PAIR_DEFAULT));
    } else {
        wattron(win, COLOR_PAIR(PAIR_RED) | A_BOLD);
        mvwprintw(win, y, 2, "✖ OFFLINE");
        wattroff(win, COLOR_PAIR(PAIR_RED) | A_BOLD);
        wattron(win, COLOR_PAIR(PAIR_DEFAULT));
        wprintw(win, "  %s", n.ip);
        wattroff(win, COLOR_PAIR(PAIR_DEFAULT));
    }

    y++;
    wattron(win, COLOR_PAIR(PAIR_CYAN) | A_DIM);
    mvwprintw(win, y, 2, "Role: %s", n.role);
    wattroff(win, COLOR_PAIR(PAIR_CYAN) | A_DIM);

    /* CPU Total */
    y += 2;
    mvwprintw(win, y, 2, "CPU Total:");
    int bar_w = w - 24;
    if (bar_w < 8) bar_w = 8;
    draw_bar(win, y, 13, bar_w, n.total_cpu_usage);

    int pct_color = PAIR_GREEN;
    if (n.total_cpu_usage >= 85.0) pct_color = PAIR_RED;
    else if (n.total_cpu_usage >= 60.0) pct_color = PAIR_YELLOW;

    wattron(win, COLOR_PAIR(pct_color) | A_BOLD);
    mvwprintw(win, y, 14 + bar_w, "%5.1f%%", n.total_cpu_usage);
    wattroff(win, COLOR_PAIR(pct_color) | A_BOLD);

    /* Load Average */
    y++;
    mvwprintw(win, y, 2, "Load Avg :");
    wattron(win, COLOR_PAIR(PAIR_YELLOW));
    wprintw(win, "  %.2f  %.2f  %.2f", n.load1, n.load5, n.load15);
    wattroff(win, COLOR_PAIR(PAIR_YELLOW));

    /* Cores Section */
    y += 2;
    wattron(win, COLOR_PAIR(PAIR_CYAN));
    mvwprintw(win, y, 2, "── Cores (%d Cores) ", n.num_cores);
    for (int i = getcurx(win); i < w - 2; i++) wprintw(win, "─");
    wattroff(win, COLOR_PAIR(PAIR_CYAN));

    y++;
    int cores = n.num_cores;
    if (cores > MAX_CORES) cores = MAX_CORES;

    if (w >= 36) {
        /* Two-column core layout */
        int half_w = (w - 4) / 2;
        int core_bar_w = half_w - 11;
        if (core_bar_w < 5) core_bar_w = 5;

        for (int i = 0; i < cores; i += 2) {
            if (y >= h - 10) break;
            /* Core i */
            mvwprintw(win, y, 2, "C%d", i);
            draw_bar(win, y, 6, core_bar_w, n.core_usage[i]);
            mvwprintw(win, y, 7 + core_bar_w, "%3.0f%%", n.core_usage[i]);

            /* Core i+1 */
            if (i + 1 < cores) {
                int col2_x = 2 + half_w;
                mvwprintw(win, y, col2_x, "C%d", i + 1);
                draw_bar(win, y, col2_x + 4, core_bar_w, n.core_usage[i + 1]);
                mvwprintw(win, y, col2_x + 5 + core_bar_w, "%3.0f%%", n.core_usage[i + 1]);
            }
            y++;
        }
    } else {
        /* Single-column core layout */
        int core_bar_w = w - 16;
        if (core_bar_w < 4) core_bar_w = 4;
        for (int i = 0; i < cores; i++) {
            if (y >= h - 10) break;
            mvwprintw(win, y, 2, "C%d", i);
            draw_bar(win, y, 6, core_bar_w, n.core_usage[i]);
            mvwprintw(win, y, 7 + core_bar_w, "%3.0f%%", n.core_usage[i]);
            y++;
        }
    }

    /* Memory Section */
    y++;
    wattron(win, COLOR_PAIR(PAIR_CYAN));
    mvwprintw(win, y, 2, "── Memory ");
    for (int i = getcurx(win); i < w - 2; i++) wprintw(win, "─");
    wattroff(win, COLOR_PAIR(PAIR_CYAN));

    y++;
    char u_str[16], t_str[16], a_str[16];
    format_kb(n.mem_used_kb, u_str, sizeof(u_str));
    format_kb(n.mem_total_kb, t_str, sizeof(t_str));
    format_kb(n.mem_avail_kb, a_str, sizeof(a_str));

    double ram_pct = 0.0;
    if (n.mem_total_kb > 0) {
        ram_pct = (100.0 * (double)n.mem_used_kb) / (double)n.mem_total_kb;
    }
    mvwprintw(win, y, 2, "RAM :");
    draw_bar(win, y, 8, bar_w, ram_pct);
    mvwprintw(win, y, 9 + bar_w, "%4.0f%%", ram_pct);

    y++;
    mvwprintw(win, y, 2, "Used: %s / %s  (Avail: %s)", u_str, t_str, a_str);

    /* Swap */
    if (n.swap_total_kb > 0 && y < h - 7) {
        y++;
        char su_str[16], st_str[16];
        format_kb(n.swap_used_kb, su_str, sizeof(su_str));
        format_kb(n.swap_total_kb, st_str, sizeof(st_str));
        double swap_pct = (100.0 * (double)n.swap_used_kb) / (double)n.swap_total_kb;
        mvwprintw(win, y, 2, "SWAP: %s / %s", su_str, st_str);
        draw_bar(win, y, 20, bar_w > 8 ? bar_w - 4 : 4, swap_pct);
    }

    /* Storage Section */
    y += 2;
    if (y < h - 4) {
        wattron(win, COLOR_PAIR(PAIR_CYAN));
        mvwprintw(win, y, 2, "── Disk Usage ");
        for (int i = getcurx(win); i < w - 2; i++) wprintw(win, "─");
        wattroff(win, COLOR_PAIR(PAIR_CYAN));

        y++;
        for (int d = 0; d < n.num_disks && y < h - 1; d++) {
            char d_tot[16], d_used[16];
            format_kb(n.disks[d].total_kb, d_tot, sizeof(d_tot));
            format_kb(n.disks[d].used_kb, d_used, sizeof(d_used));

            mvwprintw(win, y, 2, "%-9.9s", n.disks[d].mount);
            int disk_bar_w = w - 26;
            if (disk_bar_w < 5) disk_bar_w = 5;
            draw_bar(win, y, 12, disk_bar_w, (double)n.disks[d].percent);
            mvwprintw(win, y, 13 + disk_bar_w, "%s/%s (%d%%)", d_used, d_tot, n.disks[d].percent);
            y++;
        }
    }

    /* Non-blocking buffer update */
    wnoutrefresh(win);
}

void ui_render(FleetCluster *fleet) {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    /* Check if screen geometry changed */
    if (max_y != s_prev_max_y || max_x != s_prev_max_x) {
        cleanup_windows();
        clear();
        s_prev_max_y = max_y;
        s_prev_max_x = max_x;

        int content_top = 2;
        int content_h = max_y - content_top - 2;
        if (content_h < 1) content_h = 1;

        if (max_x >= 108) {
            int panel_w = max_x / 3;
            for (int i = 0; i < MAX_NODES; i++) {
                int cur_x = i * panel_w;
                int cur_w = (i == MAX_NODES - 1) ? (max_x - cur_x) : panel_w;
                s_panels[i] = newwin(content_h, cur_w, content_top, cur_x);
            }
        } else {
            int panel_h = content_h / 3;
            if (panel_h < 8) panel_h = 8;
            for (int i = 0; i < MAX_NODES; i++) {
                int cur_y = content_top + i * panel_h;
                s_panels[i] = newwin(panel_h, max_x, cur_y, 0);
            }
        }
    }

    /* Header Banner on stdscr */
    wattron(stdscr, COLOR_PAIR(PAIR_CYAN) | A_BOLD);
    mvwprintw(stdscr, 0, 2, "⚡ FLEET MONITOR");
    wattroff(stdscr, COLOR_PAIR(PAIR_CYAN) | A_BOLD);

    wattron(stdscr, COLOR_PAIR(PAIR_DEFAULT));
    wprintw(stdscr, "  │  16 Distributed Cores  │  bazina ⇄ t520 (KVM) ⇄ boxC");
    wattroff(stdscr, COLOR_PAIR(PAIR_DEFAULT));

    /* Current local time */
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_str[32];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
    mvwprintw(stdscr, 0, max_x - (int)strlen(time_str) - 2, "%s", time_str);

    /* Horizontal header separator */
    wattron(stdscr, COLOR_PAIR(PAIR_CYAN));
    for (int x = 0; x < max_x; x++) {
        mvwprintw(stdscr, 1, x, "─");
    }
    wattroff(stdscr, COLOR_PAIR(PAIR_CYAN));

    /* Calculate Cluster Totals */
    int total_cores = 0;
    int online_nodes = 0;
    double fleet_cpu_sum = 0.0;
    uint64_t fleet_mem_used = 0, fleet_mem_total = 0;

    for (int i = 0; i < MAX_NODES; i++) {
        pthread_mutex_lock(&fleet->nodes[i].lock);
        if (fleet->nodes[i].is_online) {
            online_nodes++;
            total_cores += fleet->nodes[i].num_cores;
            fleet_cpu_sum += fleet->nodes[i].total_cpu_usage;
            fleet_mem_used += fleet->nodes[i].mem_used_kb;
            fleet_mem_total += fleet->nodes[i].mem_total_kb;
        }
        pthread_mutex_unlock(&fleet->nodes[i].lock);
    }
    double fleet_cpu_avg = online_nodes > 0 ? (fleet_cpu_sum / online_nodes) : 0.0;

    /* Render Panels */
    int content_top = 2;
    int content_h = max_y - content_top - 2;
    int panel_w = (max_x >= 108) ? (max_x / 3) : max_x;

    for (int i = 0; i < MAX_NODES; i++) {
        if (s_panels[i]) {
            int cur_w = (max_x >= 108 && i == MAX_NODES - 1) ? (max_x - i * panel_w) : panel_w;
            render_node_panel(s_panels[i], &fleet->nodes[i], content_h, cur_w);
        }
    }

    /* Bottom Summary & Hotkey Bar */
    wattron(stdscr, COLOR_PAIR(PAIR_STATUSBAR));
    for (int x = 0; x < max_x; x++) {
        mvwprintw(stdscr, max_y - 1, x, " ");
    }

    char mem_u_str[16], mem_t_str[16];
    format_kb(fleet_mem_used, mem_u_str, sizeof(mem_u_str));
    format_kb(fleet_mem_total, mem_t_str, sizeof(mem_t_str));

    mvwprintw(stdscr, max_y - 1, 1,
        "FLEET: %d/%d Nodes Online │ %d/16 Cores │ CPU Avg: %.1f%% │ RAM: %s/%s │ Chrony: <1µs",
        online_nodes, MAX_NODES, total_cores, fleet_cpu_avg, mem_u_str, mem_t_str);

    const char *hotkeys = "[q] Quit  [r] Refresh";
    mvwprintw(stdscr, max_y - 1, max_x - (int)strlen(hotkeys) - 2, "%s", hotkeys);
    wattroff(stdscr, COLOR_PAIR(PAIR_STATUSBAR));

    /* Double-buffering atomic update: stdscr and panels sent in single diff */
    wnoutrefresh(stdscr);
    doupdate();
}
