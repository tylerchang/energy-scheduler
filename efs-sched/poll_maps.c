#define _GNU_SOURCE
#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>   // <-- NEW

// Must match your BPF struct definition
struct total_consumption {
    __u64 cpu_time;
    __u64 energy;
};

static volatile sig_atomic_t stop;
static int pid_filter = -1;  // < 0 means "no PID filter"
static char comm_filter[256] = {0};  // NEW: comm filter; empty = no filter

/* Which maps to show */
static int show_total       = 1;
static int show_power       = 1;
static int show_consumption = 1;
static int show_run_start   = 1;
static int show_cpu_prev    = 1;

/* Real-time baseline (monotonic since poller start) */
static struct timespec start_ts;

static void handle_sigint(int sig)
{
    (void)sig;
    stop = 1;
}

static int open_map(const char *path)
{
    int fd = bpf_obj_get(path);
    if (fd < 0) {
        fprintf(stderr, "Failed to open map %s: %s\n",
                path, strerror(errno));
    }
    return fd;
}

/* ------------ helper: does this PID match our filter? ------------------- */

static int pid_matches_filter(__u32 pid)
{
    if (pid_filter > 0 && comm_filter[0] == '\0') {
        // PID-only filter
        return pid == (unsigned)pid_filter;
    }

    if (comm_filter[0] != '\0') {
        // comm-based filter: check /proc/<pid>/comm
        char path[64];
        snprintf(path, sizeof(path), "/proc/%u/comm", pid);

        FILE *f = fopen(path, "r");
        if (!f)
            return 0;

        char buf[256];
        if (!fgets(buf, sizeof(buf), f)) {
            fclose(f);
            return 0;
        }
        fclose(f);

        // Trim trailing newline
        size_t len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n')
            buf[len - 1] = '\0';

        return strcmp(buf, comm_filter) == 0;
    }

    // No filter
    return 1;
}

/* ---------------- System totals ------------------------------------------ */

static void print_system_totals(int fd_total)
{
    struct total_consumption tot = {};
    __u32 key0 = 0;

    printf("=== System totals (total) ===\n");
    if (bpf_map_lookup_elem(fd_total, &key0, &tot) == 0) {
        printf("  cpu_time:  %llu ns\n",
               (unsigned long long)tot.cpu_time);
        printf("  energy:    %llu (units of your kfunc)\n",
               (unsigned long long)tot.energy);
    } else {
        printf("  (no data yet)\n");
    }
    printf("\n");
}

/* ---------------- PID → power (EMA) ------------------------------------- */

static void print_pid_to_power_single(int fd_power, __u32 pid)
{
    __u64 power = 0;

    printf("=== PID → power (EMA) (pid_to_power) [PID %u] ===\n", pid);
    if (bpf_map_lookup_elem(fd_power, &pid, &power) == 0) {
        printf("%-8s %-18s\n", "PID", "power");
        printf("%-8u %-18llu\n", pid, (unsigned long long)power);
    } else {
        printf("  (no entry yet for PID %u)\n", pid);
    }
    printf("\n");
}

static void print_pid_to_power_all(int fd_power)
{
    printf("=== PID → power (EMA) (pid_to_power)");
    if (comm_filter[0])
        printf(" [comm=\"%s\"]", comm_filter);
    printf(" ===\n");
    printf("%-8s %-18s\n", "PID", "power");

    __u32 prev_key = 0;
    __u32 cur_key;
    int first = 1;

    for (;;) {
        int ret;
        if (first) {
            ret = bpf_map_get_next_key(fd_power, NULL, &cur_key);
            first = 0;
        } else {
            ret = bpf_map_get_next_key(fd_power, &prev_key, &cur_key);
        }

        if (ret < 0) {
            if (errno != ENOENT && errno != 0)
                fprintf(stderr, "  bpf_map_get_next_key: %s\n",
                        strerror(errno));
            break;
        }

        __u32 pid = cur_key;
        __u64 power = 0;

        if (!pid_matches_filter(pid)) {
            prev_key = cur_key;
            continue;
        }

        if (bpf_map_lookup_elem(fd_power, &pid, &power) == 0) {
            if (power != 0) {
                printf("%-8u %-18llu\n",
                       pid,
                       (unsigned long long)power);
            }
        }

        prev_key = cur_key;
    }

    printf("\n");
}

/* ---------------- PID → cumulative energy -------------------------------- */

static void print_pid_to_consumption_single(int fd_consumption, __u32 pid)
{
    __u64 energy = 0;

    printf("=== PID → cumulative energy (pid_to_consumption) [PID %u] ===\n", pid);
    if (bpf_map_lookup_elem(fd_consumption, &pid, &energy) == 0) {
        printf("%-8s %-18s\n", "PID", "energy");
        printf("%-8u %-18llu\n", pid, (unsigned long long)energy);
    } else {
        printf("  (no entry yet for PID %u)\n", pid);
    }
    printf("\n");
}

static void print_pid_to_consumption_all(int fd_consumption)
{
    printf("=== PID → cumulative energy (pid_to_consumption)");
    if (comm_filter[0])
        printf(" [comm=\"%s\"]", comm_filter);
    printf(" ===\n");
    printf("%-8s %-18s\n", "PID", "energy");

    __u32 prev_key = 0;
    __u32 cur_key;
    int first = 1;

    for (;;) {
        int ret;
        if (first) {
            ret = bpf_map_get_next_key(fd_consumption, NULL, &cur_key);
            first = 0;
        } else {
            ret = bpf_map_get_next_key(fd_consumption, &prev_key, &cur_key);
        }

        if (ret < 0) {
            if (errno != ENOENT && errno != 0)
                fprintf(stderr, "  bpf_map_get_next_key: %s\n",
                        strerror(errno));
            break;
        }

        __u32 pid = cur_key;
        __u64 energy = 0;

        if (!pid_matches_filter(pid)) {
            prev_key = cur_key;
            continue;
        }

        if (bpf_map_lookup_elem(fd_consumption, &pid, &energy) == 0) {
            printf("%-8u %-18llu\n",
                   pid,
                   (unsigned long long)energy);
        }

        prev_key = cur_key;
    }

    printf("\n");
}

/* ---------------- PID → run start time ----------------------------------- */

static void print_pid_to_run_start_single(int fd_run_start, __u32 pid)
{
    __u64 start_time = 0;

    printf("=== PID → run start time (pid_to_run_start) [PID %u] ===\n", pid);
    if (bpf_map_lookup_elem(fd_run_start, &pid, &start_time) == 0) {
        printf("%-8s %-24s\n", "PID", "start_time_ns");
        printf("%-8u %-24llu\n", pid, (unsigned long long)start_time);
    } else {
        printf("  (no entry yet for PID %u)\n", pid);
    }
    printf("\n");
}

static void print_pid_to_run_start_all(int fd_run_start)
{
    printf("=== PID → run start time (pid_to_run_start)");
    if (comm_filter[0])
        printf(" [comm=\"%s\"]", comm_filter);
    printf(" ===\n");
    printf("%-8s %-24s\n", "PID", "start_time_ns");

    __u32 prev_key = 0;
    __u32 cur_key;
    int first = 1;

    for (;;) {
        int ret;
        if (first) {
            ret = bpf_map_get_next_key(fd_run_start, NULL, &cur_key);
            first = 0;
        } else {
            ret = bpf_map_get_next_key(fd_run_start, &prev_key, &cur_key);
        }

        if (ret < 0) {
            if (errno != ENOENT && errno != 0)
                fprintf(stderr, "  bpf_map_get_next_key: %s\n",
                        strerror(errno));
            break;
        }

        __u32 pid = cur_key;
        __u64 start_time = 0;

        if (!pid_matches_filter(pid)) {
            prev_key = cur_key;
            continue;
        }

        if (bpf_map_lookup_elem(fd_run_start, &pid, &start_time) == 0) {
            printf("%-8u %-24llu\n",
                   pid,
                   (unsigned long long)start_time);
        }

        prev_key = cur_key;
    }

    printf("\n");
}

/* ---------------- CPU → previous energy baseline ------------------------- */
/* NOTE: if cpu_to_prev_energy is PERCPU, this is only showing one slot. */

static void print_cpu_to_prev_energy(int fd_cpu_prev)
{
    printf("=== CPU → previous energy baseline (cpu_to_prev_energy) ===\n");
    printf("%-8s %-24s\n", "CPU", "prev_energy");

    __u32 prev_key = 0;
    __u32 cur_key;
    int first = 1;

    for (;;) {
        int ret;
        if (first) {
            ret = bpf_map_get_next_key(fd_cpu_prev, NULL, &cur_key);
            first = 0;
        } else {
            ret = bpf_map_get_next_key(fd_cpu_prev, &prev_key, &cur_key);
        }

        if (ret < 0) {
            if (errno != ENOENT && errno != 0)
                fprintf(stderr, "  bpf_map_get_next_key: %s\n",
                        strerror(errno));
            break;
        }

        __u32 cpu = cur_key;
        __u64 energy = 0;

        if (bpf_map_lookup_elem(fd_cpu_prev, &cpu, &energy) == 0) {
            printf("%-8u %-24llu\n",
                   cpu,
                   (unsigned long long)energy);
        }

        prev_key = cur_key;
    }

    printf("\n");
}

/* ---------------- CLI parsing helpers ------------------------------------ */

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage:\n"
            "  %s [interval_ms] [pid_filter|comm=NAME|-] [map ...]\n"
            "\n"
            "  interval_ms  : polling interval in milliseconds (default: 1000)\n"
            "  pid_filter   : PID to filter on (0 or '-' for none)\n"
            "                 Or 'comm=NAME' to filter by /proc/<pid>/comm\n"
            "  map          : one or more of:\n"
            "                 total, power, consumption, run_start, cpu_prev\n"
            "                 If no maps are listed, all are shown.\n"
            "\n"
            "Examples:\n"
            "  %s                    # 1000 ms, no PID/comm filter, all maps\n"
            "  %s 500                # 500 ms, no PID/comm filter, all maps\n"
            "  %s 1000 12345         # 1s, PID=12345, all maps\n"
            "  %s 1000 comm=mem_test power consumption\n"
            "  %s 1000 - total cpu_prev\n",
            prog, prog, prog, prog, prog, prog);
}

static void init_map_filters_from_args(int argc, char **argv, int start_idx)
{
    /* If no map names supplied, keep defaults (all 1). */
    if (start_idx >= argc)
        return;

    /* Otherwise, reset all to 0 and enable only requested ones. */
    show_total       = 0;
    show_power       = 0;
    show_consumption = 0;
    show_run_start   = 0;
    show_cpu_prev    = 0;

    for (int i = start_idx; i < argc; i++) {
        const char *arg = argv[i];

        if (strcmp(arg, "total") == 0)
            show_total = 1;
        else if (strcmp(arg, "power") == 0)
            show_power = 1;
        else if (strcmp(arg, "consumption") == 0)
            show_consumption = 1;
        else if (strcmp(arg, "run_start") == 0)
            show_run_start = 1;
        else if (strcmp(arg, "cpu_prev") == 0)
            show_cpu_prev = 1;
        else {
            fprintf(stderr, "Unknown map name '%s', ignoring.\n", arg);
        }
    }

    /* If user gave only unknown names, revert to showing all. */
    if (!show_total && !show_power && !show_consumption &&
        !show_run_start && !show_cpu_prev) {
        show_total       = 1;
        show_power       = 1;
        show_consumption = 1;
        show_run_start   = 1;
        show_cpu_prev    = 1;
    }
}

/* ------------------------------------------------------------------------- */

int main(int argc, char **argv)
{
    const char *total_path        = "/sys/fs/bpf/efs/total";
    const char *power_path        = "/sys/fs/bpf/efs/pid_to_power";
    const char *consumption_path  = "/sys/fs/bpf/efs/pid_to_consumption";
    const char *run_start_path    = "/sys/fs/bpf/efs/pid_to_run_start";
    const char *cpu_prev_path     = "/sys/fs/bpf/efs/cpu_to_prev_energy";

    int interval_ms = 1000;   // now explicitly milliseconds

    if (argc > 1 && strcmp(argv[1], "-h") == 0) {
        usage(argv[0]);
        return 0;
    }

    /*
     * CLI:
     *   poll_maps
     *   poll_maps 500
     *   poll_maps 1000 12345
     *   poll_maps 1000 comm=mem_test power consumption
     *   poll_maps 1000 - total cpu_prev
     */
    int argi = 1;

    if (argi < argc) {
        interval_ms = atoi(argv[argi]);
        if (interval_ms <= 0)
            interval_ms = 1000;
        argi++;
    }

    if (argi < argc) {
        if (strcmp(argv[argi], "-") == 0) {
            pid_filter = -1;
            comm_filter[0] = '\0';
        } else if (strncmp(argv[argi], "comm=", 5) == 0) {  // NEW
            pid_filter = -1;
            strncpy(comm_filter, argv[argi] + 5, sizeof(comm_filter) - 1);
            comm_filter[sizeof(comm_filter) - 1] = '\0';
        } else {
            pid_filter = atoi(argv[argi]);
            if (pid_filter <= 0)
                pid_filter = -1;
            comm_filter[0] = '\0';
        }
        argi++;
    }

    /* Parse map filters from remaining args. */
    init_map_filters_from_args(argc, argv, argi);

    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigint);

    int fd_total       = open_map(total_path);
    int fd_power       = open_map(power_path);
    int fd_consumption = open_map(consumption_path);
    int fd_run_start   = open_map(run_start_path);
    int fd_cpu_prev    = open_map(cpu_prev_path);

    if (fd_total < 0 || fd_power < 0 ||
        fd_consumption < 0 || fd_run_start < 0 || fd_cpu_prev < 0) {
        fprintf(stderr,
                "Failed to open one or more maps; "
                "make sure they are pinned at /sys/fs/bpf/efs/.\n");
        return 1;
    }

    /* Initialize real-time baseline */
    clock_gettime(CLOCK_MONOTONIC, &start_ts);

    printf("Polling energy-fair scheduler maps every %d millisecond(s)...\n",
           interval_ms);
    if (pid_filter > 0)
        printf("Filtering on PID %d\n", pid_filter);
    if (comm_filter[0])
        printf("Filtering on comm \"%s\"\n", comm_filter);
    printf("Maps shown: ");
    if (show_total)       printf("total ");
    if (show_power)       printf("power ");
    if (show_consumption) printf("consumption ");
    if (show_run_start)   printf("run_start ");
    if (show_cpu_prev)    printf("cpu_prev ");
    printf("\n");
    printf("Press Ctrl+C to stop.\n\n");

    while (!stop) {
        /* top-style: clear screen each iteration */
        printf("\033[H\033[J");

        /* Compute real-time since start */
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        double elapsed_real =
            (now.tv_sec - start_ts.tv_sec) +
            (now.tv_nsec - start_ts.tv_nsec) / 1e9;

        printf("Polling energy-fair scheduler maps every %d millisecond(s)...\n",
               interval_ms);
        printf("Real time since poller start: %.6f s\n", elapsed_real);
        if (pid_filter > 0)
            printf("Filter: PID %d\n", pid_filter);
        if (comm_filter[0])
            printf("Filter: comm \"%s\"\n", comm_filter);

        printf("Maps shown: ");
        if (show_total)       printf("total ");
        if (show_power)       printf("power ");
        if (show_consumption) printf("consumption ");
        if (show_run_start)   printf("run_start ");
        if (show_cpu_prev)    printf("cpu_prev ");
        printf("\n\n");

        if (show_total)
            print_system_totals(fd_total);

        if (show_power) {
            if (pid_filter > 0 && comm_filter[0] == '\0')
                print_pid_to_power_single(fd_power, (unsigned)pid_filter);
            else
                print_pid_to_power_all(fd_power);
        }

        if (show_consumption) {
            if (pid_filter > 0 && comm_filter[0] == '\0')
                print_pid_to_consumption_single(fd_consumption, (unsigned)pid_filter);
            else
                print_pid_to_consumption_all(fd_consumption);
        }

        if (show_run_start) {
            if (pid_filter > 0 && comm_filter[0] == '\0')
                print_pid_to_run_start_single(fd_run_start, (unsigned)pid_filter);
            else
                print_pid_to_run_start_all(fd_run_start);
        }

        if (show_cpu_prev)
            print_cpu_to_prev_energy(fd_cpu_prev);

        fflush(stdout);
        usleep(interval_ms * 1000);  // milliseconds → microseconds
    }

    close(fd_total);
    close(fd_power);
    close(fd_consumption);
    close(fd_run_start);
    close(fd_cpu_prev);

    return 0;
}
