# fleetMonitor ⚡

High-performance, multi-threaded C ncurses monitor for the distributed fleet:
`[bazina] <---> [t520 (KVM Server)] <---> [boxC (Storage Node)]`

Monitors **16 distributed cores**, memory, load averages, and storage volumes across all cluster nodes in real-time with sub-millisecond precision.

---

## Architecture & Fleet Layout

The UI automatically lays out the nodes to match the physical desktop arrangement:

```
┌───────────────────────────┬───────────────────────────┬───────────────────────────┐
│       bazina (LEFT)       │     t520 (CENTER/KVM)     │      boxC (RIGHT/NFS)     │
│       Debian 13 Trixie    │     Arch/Manjaro Linux    │     Raspberry Pi OS 12    │
│       10.0.0.15           │     10.0.0.18             │     10.0.0.14             │
│       4 Cores (Navi 10)   │     8 Cores               │     4 Cores (aarch64)     │
└───────────────────────────┴───────────────────────────┴───────────────────────────┘
```

- **Asynchronous Collectors**: Each node is polled in its own dedicated background `pthread`. The ncurses UI stays completely non-blocking and buttery smooth (20 FPS redraw).
- **SSH Multiplexing**: Queries remote nodes (`bazina`, `boxC`) over persistent SSH ControlMaster sessions with zero handshake overhead (< 20ms RTT).
- **Local Sampling**: Reads `/proc/stat`, `/proc/meminfo`, `/proc/loadavg`, and filesystem structures directly on the local node.
- **Adaptive Layout**: Automatically switches between a 3-column panoramic layout (on wide terminals >= 108 columns) and a stacked card layout on smaller screens.
- **Precision Metrics**:
  - Per-core CPU utilization percentage calculated from `/proc/stat` delta tick counts.
  - Overall CPU load with color-coded gradients (green < 60%, yellow 60–85%, red > 85%).
  - 1m, 5m, 15m load averages.
  - RAM used, total, and available memory + Swap gauges.
  - Disk mount usage (`/` and `/mnt/data` or `/mnt/kof17`).
  - Active node RTT latency and online/offline status indicators.

---

## Build & Installation

### Dependencies
- C99/GNU99 compatible compiler (`gcc` or `clang`)
- `libncursesw5-dev` / `ncurses` (wide-character support)
- POSIX threads (`pthread`)

### Build
```bash
make
```

### Install
Installs binary to `~/bin/fleetMonitor` with symlink `fleet-monitor`:
```bash
make install
```

---

## Usage

```bash
fleetMonitor [OPTIONS]
```

### Options
- `-r <sec>`: Refresh interval in seconds (default: `1.0`)
- `-h, --help`: Show help message
- `-v`: Show version information

### Keybindings
- `q`, `Q`, `ESC`: Clean exit and restore terminal
- `KEY_RESIZE`: Automatic terminal reflow
