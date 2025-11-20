#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

// --- Constants ---------------------------------------------------------------

#define SHARED_DSQ_ID 0
#define TOTAL_CPU_TIME 0
#define TOTAL_SYSTEM_ENERGY 1

#define BPF_STRUCT_OPS(name, args...)	\
    SEC("struct_ops/"#name)	BPF_PROG(name, ##args)

#define BPF_STRUCT_OPS_SLEEPABLE(name, args...)	\
    SEC("struct_ops.s/"#name)							      \
    BPF_PROG(name, ##args)

// --- BPF MAPS ----------------------------------------------------------------

// Total system CPU time + energy accumulated
struct total_consumption {
    u64 cpu_time;
    u64 energy;
};

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, u32);
    __type(value, struct total_consumption);
} total SEC(".maps");

// PID → cumulative energy consumption
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 4096);
    __type(key, u32);
    __type(value, u64);
} pid_to_consumption SEC(".maps");

// PID → estimated power (EMA)
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 4096);
    __type(key, u32);
    __type(value, u64);
} pid_to_power SEC(".maps");

// PID → start time of most recent ON-CPU execution
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 4096);
    __type(key, u32);
    __type(value, u64);
} pid_to_run_start SEC(".maps");

// CPU → last cumulative energy reading
struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_HASH);
    __uint(max_entries, 1024);
    __type(key, u32);
    __type(value, u64);
} cpu_to_prev_energy SEC(".maps");

extern u64 read_core_energy(int cpu) __ksym;

// ----------------------------------------------------------------------------
//  sched_init: create shared DSQ
// ----------------------------------------------------------------------------
s32 BPF_STRUCT_OPS_SLEEPABLE(sched_init)
{
    return scx_bpf_create_dsq(SHARED_DSQ_ID, -1);
}

// ----------------------------------------------------------------------------
//  sched_enqueue: compute venergy and dispatch into priority DSQ
// ----------------------------------------------------------------------------
s32 BPF_STRUCT_OPS(sched_enqueue, struct task_struct *p, u64 flags)
{
    u32 key = 0;
    struct total_consumption *tot = bpf_map_lookup_elem(&total, &key);
    if (!tot || tot->cpu_time == 0)
        return 0;

    u32 pid = p->pid;

    // 1. Compute system_vpower
    u64 system_vpower = tot->energy / tot->cpu_time;

    // 2. Lookup per-PID power
    u64 *ppower = bpf_map_lookup_elem(&pid_to_power, &pid);
    if (!ppower)
        return 0;

    // vpower = pid_power / system_vpower
    u64 vpower = *ppower / system_vpower;

    // 3. Read vruntime (from CFS struct)
    u64 vruntime = BPF_CORE_READ(p, se.vruntime);
    u64 venergy  = vruntime * vpower;

    // 4. Insert into shared DSQ ordered by venergy
    scx_bpf_dsq_insert_vtime(p, venergy, 0, SHARED_DSQ_ID, flags);

    return 0;
}

// ----------------------------------------------------------------------------
//  sched_switch: compute energy-based power estimate, update state
// ----------------------------------------------------------------------------
SEC("tp_btf/sched_switch")
int BPF_PROG(handle_sched_switch,
             struct task_struct *prev,
             struct task_struct *next)
{
    u64 now  = bpf_ktime_get_ns();
    u32 cpu  = bpf_get_smp_processor_id();
    u32 prev_pid = BPF_CORE_READ(prev, pid);
    u32 next_pid = BPF_CORE_READ(next, pid);

    // load previous start time
    u64 *prev_start = bpf_map_lookup_elem(&pid_to_run_start, &prev_pid);
    if (!prev_start) {
        // this CPU had no prev running → just set next start time
        bpf_map_update_elem(&pid_to_run_start, &next_pid, &now, BPF_ANY);
        return 0;
    }

    // load previous cumulative energy for this CPU
    u64 *prev_energy = bpf_map_lookup_elem(&cpu_to_prev_energy, &cpu);
    if (!prev_energy) {
        u64 cur = read_core_energy(cpu);
        bpf_map_update_elem(&cpu_to_prev_energy, &cpu, &cur, BPF_ANY);
        return 0;
    }

    // read current cumulative energy
    u64 cur_energy = read_core_energy(cpu);

    // energy consumed by prev
    u64 delta_energy = cur_energy - *prev_energy;
    u64 delta_time   = now - *prev_start;

    if (delta_time == 0)
        delta_time = 1;  // avoid div-by-zero

    // instantaneous power = ΔE / Δt
    u64 new_power = delta_energy / delta_time;

    // update EMA power
    u64 old_power = 0;
    u64 *pp = bpf_map_lookup_elem(&pid_to_power, &prev_pid);
    if (pp)
        old_power = *pp;

    u64 ema = 1000 * ((old_power / 2) + (new_power / 2));
    bpf_map_update_elem(&pid_to_power, &prev_pid, &ema, BPF_ANY);

    // update cumulative per-PID energy
    u64 *cons = bpf_map_lookup_elem(&pid_to_consumption, &prev_pid);
    u64 new_val;
    if (cons)
        new_val = *cons + delta_energy;
    else   
        new_val = delta_energy;

    bpf_map_update_elem(&pid_to_consumption, &prev_pid, &new_val, BPF_ANY);

    // update CPU energy snapshot
    bpf_map_update_elem(&cpu_to_prev_energy, &cpu, &cur_energy, BPF_ANY);

    // update next start time
    bpf_map_update_elem(&pid_to_run_start, &next_pid, &now, BPF_ANY);

    return 0;
}

// ----------------------------------------------------------------------------
//  exit_task: cleanup dead PID state
// ----------------------------------------------------------------------------
// SEC("tp_btf/task_free")
// int BPF_PROG(exit_task, struct task_struct *p)
// {
//     u32 pid = p->pid;

//     bpf_map_delete_elem(&pid_to_consumption, &pid);
//     bpf_map_delete_elem(&pid_to_power, &pid);
//     bpf_map_delete_elem(&pid_to_run_start, &pid);

//     return 0;
// }

// ----------------------------------------------------------------------------
//  Registration
// ----------------------------------------------------------------------------

SEC(".struct_ops.link")
struct sched_ext_ops sched_ops = {
    .init      = (void *)sched_init,
    .enqueue   = (void *)sched_enqueue,
    .flags     = SCX_OPS_ENQ_LAST | SCX_OPS_KEEP_BUILTIN_IDLE,
    .name      = "energy_fair_scheduler",
};

char _license[] SEC("license") = "GPL";