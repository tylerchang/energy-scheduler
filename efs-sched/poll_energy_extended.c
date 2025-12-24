#define _GNU_SOURCE
#include <bpf/bpf.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ================= Configuration ================= */

#define MAX_WORKLOADS 8
#define TASK_COMM_LEN 16

/* ================= BPF map structs ================= */

struct total_consumption {
    uint64_t cpu_time;
    uint64_t energy;
    uint64_t uncore_energy;
};

/* ================= Globals ================= */

static volatile sig_atomic_t stop;

static char workload_prefixes[MAX_WORKLOADS][TASK_COMM_LEN];
static int nr_workloads = 0;

/* ================= Signal handling ================= */

static void handle_sigint(int sig)
{
    (void)sig;
    stop = 1;
}

/* ================= Helpers ================= */

static int open_map(const char *path)
{
    int fd = bpf_obj_get(path);
    if (fd < 0) {
        fprintf(stderr, "Failed to open map %s: %s\n",
                path, strerror(errno));
    }
    return fd;
}

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static int get_comm_for_pid(uint32_t pid, char *buf)
{
    char path[64];
    snprintf(path, sizeof(path), "/proc/%u/comm", pid);

    FILE *f = fopen(path, "r");
    if (!f)
        return -1;

    if (!fgets(buf, TASK_COMM_LEN, f)) {
        fclose(f);
        return -1;
    }
    fclose(f);

    size_t len = strlen(buf);
    if (len && buf[len - 1] == '\n')
        buf[len - 1] = '\0';

    return 0;
}

static int is_workload_comm(const char *comm)
{
    if (!comm || comm[0] == '\0' || comm[0] == '?')
        return 0;

    if (nr_workloads == 0)
        return 1;

    for (int i = 0; i < nr_workloads; i++) {
        size_t len = strlen(workload_prefixes[i]);
        if (strncmp(comm, workload_prefixes[i], len) == 0)
            return 1;
    }
    return 0;
}

/* ================= Main ================= */

int main(int argc, char **argv)
{
    const char *total_path = "/sys/fs/bpf/efs/total";
    const char *pid_power_path = "/sys/fs/bpf/efs/pid_to_power";
    const char *pid_energy_path = "/sys/fs/bpf/efs/pid_to_consumption";

    int interval_ms = 20;

    /* -------- CLI parsing -------- */

    int argi = 1;
    if (argi < argc) {
        interval_ms = atoi(argv[argi]);
        if (interval_ms <= 0)
            interval_ms = 20;
        argi++;
    }

    if (argi < argc && strncmp(argv[argi], "workload=", 9) == 0) {
        char *p = argv[argi] + 9;
        char *tok;

        while ((tok = strsep(&p, ",")) && nr_workloads < MAX_WORKLOADS) {
            strncpy(workload_prefixes[nr_workloads],
                    tok,
                    TASK_COMM_LEN - 1);
            workload_prefixes[nr_workloads][TASK_COMM_LEN - 1] = '\0';
            nr_workloads++;
        }
        argi++;
    }

    /* -------- Open maps -------- */

    int fd_total = open_map(total_path);
    int fd_power = open_map(pid_power_path);
    int fd_energy = open_map(pid_energy_path);

    if (fd_total < 0 || fd_power < 0 || fd_energy < 0) {
        fprintf(stderr, "Failed to open one or more BPF maps\n");
        return 1;
    }

    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigint);

    /* -------- CSV header -------- */

    printf(
        "time_s,"
        "pid,"
        "comm,"
        "pid_power,"
        "pid_energy_j,"
        "sys_cpu_time_ns,"
        "sys_energy_j,"
        "sys_uncore_energy_j\n"
    );
    fflush(stdout);

    /* -------- Poll loop -------- */

    while (!stop) {
        double t = now_s();

        /* ---- System totals ---- */

        struct total_consumption tot = {};
        uint32_t key0 = 0;

        if (bpf_map_lookup_elem(fd_total, &key0, &tot) == 0) {
            printf(
                "%.6f,"
                "0,"
                "system,"
                "0,"
                "0,"
                "%llu,"
                "%llu,"
                "%llu\n",
                t,
                (unsigned long long)tot.cpu_time,
                (unsigned long long)tot.energy,
                (unsigned long long)tot.uncore_energy
            );
        }

        /* ---- Per-PID entries ---- */

        uint32_t prev = 0, pid;
        int first = 1;

        for (;;) {
            int ret = first
                ? bpf_map_get_next_key(fd_energy, NULL, &pid)
                : bpf_map_get_next_key(fd_energy, &prev, &pid);

            if (ret < 0)
                break;

            first = 0;
            prev = pid;

            char comm[TASK_COMM_LEN] = {};
            if (get_comm_for_pid(pid, comm) < 0)
                continue;

            if (!is_workload_comm(comm))
                continue;

            uint64_t energy = 0;
            uint64_t power = 0;

            bpf_map_lookup_elem(fd_energy, &pid, &energy);
            bpf_map_lookup_elem(fd_power, &pid, &power);

            printf(
                "%.6f,"
                "%u,"
                "%s,"
                "%llu,"
                "%llu,"
                "0,0,0\n",
                t,
                pid,
                comm,
                (unsigned long long)power,
                (unsigned long long)energy
            );
        }

        fflush(stdout);
        usleep(interval_ms * 1000);
    }

    close(fd_total);
    close(fd_power);
    close(fd_energy);
    return 0;
}
