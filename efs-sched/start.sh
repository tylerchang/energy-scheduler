#!/bin/sh

# Usage: ./start.sh scheduler_file.c

# Build the scheduler if the C file is younger than the .c.o file or if the .c.o file doesn't exist
# use sched_ext.bpf.c as default
C_FILE=${1:-efs.bpf.c}

# if --help is passed, print the usage

if [ "$1" = "--help" ]; then
    echo "Usage: ./start.sh scheduler_file.c"
    # print all the available scheduler files in the directory
    echo "Available scheduler files:"
    ls -1 *.bpf.c
    exit 0
fi

./build.sh $1

sudo ./stop.sh 


# Register the scheduler
sudo bpftool struct_ops register ${C_FILE}.o /sys/fs/bpf/sched_ext || (echo "Error attaching scheduler, consider calling stop.sh before" || exit 1)

# Print scheduler name, fails if it isn't registered properly
cat /sys/kernel/sched_ext/root/ops || (echo "No sched-ext scheduler installed" && exit 1)


# Pin the maps
sudo mkdir -p /sys/fs/bpf/efs
sudo bpftool map pin name total /sys/fs/bpf/efs/total
sudo bpftool map pin name pid_to_consumpt /sys/fs/bpf/efs/pid_to_consumption
sudo bpftool map pin name pid_to_power /sys/fs/bpf/efs/pid_to_power
sudo bpftool map pin name cpu_to_prev_ene /sys/fs/bpf/efs/cpu_to_prev_energy
sudo bpftool map pin name pid_to_run_star /sys/fs/bpf/efs/pid_to_run_start
sudo bpftool map pin name pid_to_runtime /sys/fs/bpf/efs/pid_to_runtime
sudo bpftool map pin name pid_to_dispatch /sys/fs/bpf/efs/pid_to_dispatches
sudo bpftool map pin name pid_to_enqueue_ /sys/fs/bpf/efs/pid_to_enqueue_time
sudo bpftool map pin name pid_to_wait_tim /sys/fs/bpf/efs/pid_to_wait_time
sudo bpftool map pin name pid_to_last_cpu /sys/fs/bpf/efs/pid_to_last_cpu
sudo bpftool map pin name cpu_to_prev_pkg /sys/fs/bpf/efs/cpu_to_prev_pkg
