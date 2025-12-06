#!/bin/sh

# Remove the scheduler
sudo rm /sys/fs/bpf/sched_ext/sched_ops

# Remove the pinned maps
sudo rm -rf /sys/fs/bpf/efs
