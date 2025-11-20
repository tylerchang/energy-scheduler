# Multi-Core Energy-Aware Process Scheduling

## Hardware Setup
AMD CPU with per-core RAPL readings

CloudLab Machine Type: r6615

## Ubuntu Environment Setup
Currently, only release 25.04 and newer are supported. If you're using an earlier release, upgrade using the command below:
```
sudo do-release-upgrade -d
```

## Setup Developer Environment
```
sudo apt install -y build-essential cmake cargo rustc clang llvm pkg-config libelf-dev protobuf-compiler libseccomp-dev libbpf-dev
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

### RAPL Energy kfunc

For eBPF to read the AMD RAPL energy counters, we must expose a kfunc that allows it do so. We do this by installing a kernel module containing this new kfunc. Do this by:

```
cd 
```


