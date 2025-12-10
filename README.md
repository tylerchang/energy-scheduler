# Multi-Core Energy-Aware Process Scheduling

## Hardware Setup
AMD CPU with per-core RAPL readings

CloudLab Machine Type: r6615 or d7615

- Should work with AMD EPYC 9xxx CPUs as they are part of the Zen 4 Genoa series, which was what the per-core RAPL Linux patch was developed on according to this [article](https://lwn.net/Articles/981655/)
- Cloudlab provides a [hardware list](https://docs.cloudlab.us/hardware.html) specifying which models contain AMD CPUs
- Here is the [availability chart](https://www.cloudlab.us/resinfo.php) for hardware when reserving

## Ubuntu Environment Setup
Currently, only release 25.04 and newer are supported. If you're using an earlier release, upgrade using the command below:
```
sudo do-release-upgrade -d
```

## Setup Developer Environment
```
sudo apt install -y build-essential cmake cargo rustc clang llvm pkg-config libelf-dev protobuf-compiler libseccomp-dev libbpf-dev dwarves
```


## Test If Machine Supports Package-Level RAPL Readings 
We can use the `msr` kernel module to read package-level RAPL readings

```
sudo apt update
sudo apt install msr-tools
sudo modprobe msr
lsmod | grep msr # tells you if msr is active
sudo rdmsr 0xC001029B # read the package-level energy counter
```

## Reading Per-Core RAPL Readings with amd_energy
### Install `amd_energy`
```
git clone git@github.com:amd/amd_energy.git
cd amd_energy
make
sudo make modules_install
sudo depmod -a
sudo modprobe amd_energy
```

### Reading
Perform the following command to list all the devices that have energy readings available. You should see multiple directories called `hwmon<number>`
```
ls /sys/class/hwmon/
```

To find which hwmon directory refers to your CPU, do:
```
cat /sys/class/hwmon/hwmon*/name
```

Let's say `hwmon7` refers to our CPU, traverse to that folder:
```
cd /sys/class/hwmon/hwmon7
```

In here, you should see all the available cores for reading. Reading the `_label` will tell you the name of the core and reading the `_input` will tell you the energy readings in Joules.

https://elixir.bootlin.com/linux/v6.17/source/tools/testing/selftests/sched_ext - implementation and examples of sched_ext stuff.
https://elixir.bootlin.com/linux/v6.17.8/source/include/trace/events/sched.h#L220 - for useful scheduling tracepoints
https://elixir.bootlin.com/linux/v6.17/source/tools/testing/selftests/sched_ext/maximal.bpf.c - For seeing absolutely everything that sched_ext can hook onto.

### RAPL Energy kfunc

For eBPF to read the AMD RAPL energy counters, we must expose a kfunc that allows it do so. We do this by installing a kernel module containing this new kfunc. Do this by:

```
cd energy_kfunc_module/module
sudo cp /sys/kernel/btf/vmlinux /usr/lib/modules/`uname -r`/build/
make
sudo insmod read_core_energy
```

### Running the Scheduler
```
cd minimal-sched/
sudo ./build.sh
sudo ./start.sh
```

You can check that the scheduler is running using:
```
cat /sys/kernel/sched_ext/root/ops
```

To stop the scheduler, run
```
sudo ./stop.sh
```



To observe trace logs of the scheduler:
```
sudo bpftool prog tracelog
```

In order to read from the eBPF maps exposed by the EFS scheduler
```
sudo mkdir -p /sys/fs/bpf/efs
sudo bpftool map pin id 115 /sys/fs/bpf/efs/total
sudo bpftool map pin id 116 /sys/fs/bpf/efs/pid_to_consumption
sudo bpftool map pin id 117 /sys/fs/bpf/efs/pid_to_power
sudo bpftool map pin id 119 /sys/fs/bpf/efs/cpu_to_prev_energy
```
And then run the userspace program:
```
./poll_maps
```

# Useful Commands
`sudo ./poll_maps 100 comm=mem_miss power consumption | tee out.log`
`python plot_power.py out.log graph.png --wall`

# Current TODOs
1. Figure out why rolling pid_to_power keeps increasing for a given process (stress_core)
2. Reunderstand sched-running and sched-stop and confirm math
3. Workloads 
    - [x] Memory intensive process doing random reads / writes
    - [x] CPU intensive process doing SHA hashing computation

# Plans for Slides
1. Intro + HotCarbon Limitations + Per-Process Energy Readings over Multi-Core
2. Poll Map Output Demonstration (all the maps)
3. Two workloads + Graphs for both
4. Varying-Core Effects on Energy. Given a constant number of processes, observe energy usage during high concurrency vs parallelism
5. Findings + Future Work




