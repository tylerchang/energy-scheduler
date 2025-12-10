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

#ifndef PF_KTHREAD
#define PF_KTHREAD 0x00200000
#endif

const volatile char debug_prog[TASK_COMM_LEN] = "stress_core";
const volatile bool debug_enabled = true;

static __always_inline bool debug_task(const struct task_struct *p)
{
    if (!debug_enabled)
        return false;

    char comm[TASK_COMM_LEN];

    // Read p->comm safely
    if (bpf_core_read_str(&comm, sizeof(comm), &p->comm) < 0)
        return false;

    // compare with debug_prog, up to TASK_COMM_LEN
    #pragma unroll
    for (int i = 0; i < TASK_COMM_LEN; i++) {
        if (debug_prog[i] == '\0' && comm[i] == '\0')
            return true;                 // exact match
        if (debug_prog[i] != comm[i])
            return false;
    }
    return true;
}

#define DBG(p, fmt, ...)                                \
    do {                                                \
        if (debug_task(p))                              \
            bpf_printk(fmt, ##__VA_ARGS__);             \
    } while (0)

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
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 1024);
    __type(key, u32);
    __type(value, u64);
} cpu_to_prev_energy SEC(".maps");

extern u64 read_core_energy(void) __ksym;

// ----------------------------------------------------------------------------
//  sched_init: create shared DSQ
// ----------------------------------------------------------------------------
s32 BPF_STRUCT_OPS_SLEEPABLE(sched_init)
{   
    bpf_printk("EFS SCHED INIT HAS BEEN CALLED\n");
    return scx_bpf_create_dsq(SHARED_DSQ_ID, -1);
}

// ----------------------------------------------------------------------------
//  sched_enqueue: compute venergy and dispatch into priority DSQ
// ----------------------------------------------------------------------------
s32 BPF_STRUCT_OPS(sched_enqueue, struct task_struct *p, u64 flags)
{

    if (p->flags & PF_KTHREAD) {
        // Kernel threads go to the global DSQ (can run on any CPU)
        u64 slice = 5000000u;
//        bpf_printk("%s is being scheduled using GLOBAL QUEUE\n", p->comm);
        scx_bpf_dsq_insert(p, SCX_DSQ_GLOBAL, slice, flags);
        return 0;
    }
    DBG(p, "[sched_enqueue]: %s scheduled using EFS\n", p->comm);
    u32 pid = p->pid;
    u64 vpower = 1;  // default

    // Optional: normalize by system totals if available
    u32 key = 0;
    struct total_consumption *tot = bpf_map_lookup_elem(&total, &key);
    u64 system_power = 1;

    if (tot && tot->cpu_time) {
        u64 tmp = tot->energy / tot->cpu_time;
        if (tmp > 0)
            system_power = tmp;
    }

    u64 *ppower = bpf_map_lookup_elem(&pid_to_power, &pid);
    if (ppower && *ppower)
        vpower = *ppower / system_power;   // if system_power==1, this is just raw ppower

    // 3. Read vruntime (from CFS struct)
    u64 vruntime = BPF_CORE_READ(p, se.vruntime);
    u64 venergy  = vruntime * vpower;

    // 4. Insert into shared DSQ ordered by venergy
    scx_bpf_dsq_insert_vtime(p, SHARED_DSQ_ID, 0, venergy, flags);
    

    return 0;
}



void BPF_STRUCT_OPS(sched_dispatch, s32 cpu, struct task_struct *prev)
{
    // bpf_printk("[sched_dispatch] CPU %d called dispatch.", cpu);
	scx_bpf_dsq_move_to_local(SHARED_DSQ_ID);
}


void BPF_STRUCT_OPS(sched_exit_task, struct task_struct *p, struct scx_exit_task_args *args){
    u32 pid = p->pid;
    int zero = 0;
    bpf_map_update_elem(&pid_to_power, &pid, &zero, BPF_ANY);
    DBG(p, "EXIT: PID=%d power set to 0\n", p->pid);
}

// void BPF_STRUCT_OPS(sched_runnable, struct task_struct *p, u64 flags) {
//     u32 pid = p->pid;
//     u64 zero = 0;
//     if (flags & SCX_ENQ_WAKEUP)
//         bpf_map_update_elem(&pid_to_power, &pid, &zero, BPF_ANY);
// }

// ----------------------------------------------------------------------------
//  sched_running: task just started running on this CPU
//  - record start timestamp and baseline energy for this CPU
// ----------------------------------------------------------------------------
void BPF_STRUCT_OPS(sched_running, struct task_struct *p)
{
    if (p->flags & PF_KTHREAD)
        return;

    u32 pid = p->pid;
    u32 cpu = bpf_get_smp_processor_id();
    u64 now = bpf_ktime_get_ns();
    u64 cur_energy = read_core_energy();

    DBG(p, "[RUNNING]: comm=%s pid=%d cpu=%d cur_energy:%llu\n", debug_prog, p->pid, cpu, cur_energy);

    // bpf_printk("RUNNING: PID=%d cpu=%d, cur_energy:%llu\n",pid,cpu,cur_energy);    

    // Remember when this PID started running
    bpf_map_update_elem(&pid_to_run_start, &pid, &now, BPF_ANY);

    // Remember baseline energy for this CPU
    bpf_map_update_elem(&cpu_to_prev_energy, &cpu, &cur_energy, BPF_ANY);

}

// ----------------------------------------------------------------------------
//  sched_stopping: task just stopped running on this CPU
//  - compute Δt and ΔE, update:
//      * pid_to_power (EMA power)
//      * pid_to_consumption (cumulative energy)
//      * total (system totals)
//      * cpu_to_prev_energy baseline
// ----------------------------------------------------------------------------
void BPF_STRUCT_OPS(sched_stopping, struct task_struct *p, bool runnable)
{
    if (p->flags & PF_KTHREAD)
        return;

    u32 pid = p->pid;
    u32 cpu = bpf_get_smp_processor_id();
    u64 now = bpf_ktime_get_ns();

    u64 *start = bpf_map_lookup_elem(&pid_to_run_start, &pid);
    u64 *prev_energy = bpf_map_lookup_elem(&cpu_to_prev_energy, &cpu);
    if (!start)
        return;

    u64 cur_energy = read_core_energy();

    if(!prev_energy){
        DBG(p,"STOPPING: cpu %d not found in cpu_to_prev\n", cpu);
        bpf_map_update_elem(&cpu_to_prev_energy, &cpu, &cur_energy, BPF_ANY);
        return;
    }

    u64 delta_time = now - *start;
    if (delta_time == 0)
        delta_time = 1; // avoid div-by-zero


    if (*prev_energy == cur_energy){
        return;
    }
    u64 delta_energy = (cur_energy - *prev_energy) * 10000;

    // instantaneous power over this run interval
    u64 new_power = (delta_energy / delta_time);

    // update EMA power
    u64 old_power = 0;
    u64 *pp = bpf_map_lookup_elem(&pid_to_power, &pid);
    if (pp)
        old_power = *pp;

      DBG(p,"[STOPPING]: comm=%s, PID=%d cpu=%d old=%llu new=%llu dE=%llu dT=%llu prevE=%llu curE=%llu\n",
           p->comm,
           pid,
           cpu,
           old_power,
           new_power,
           delta_energy,
           delta_time,
           *prev_energy,
           cur_energy);    
    u64 ema = ((old_power / 2) + (new_power / 2));
    bpf_map_update_elem(&pid_to_power, &pid, &ema, BPF_ANY);
    DBG(p,"STOPPING: pid_to_power updated, PID: %d, EMA: %d\n", pid, ema);


    // update cumulative per-PID energy
    u64 *cons = bpf_map_lookup_elem(&pid_to_consumption, &pid);
    u64 new_val;
    if (cons)
        new_val = *cons + delta_energy;
    else
        new_val = delta_energy;

    bpf_map_update_elem(&pid_to_consumption, &pid, &new_val, BPF_ANY);
    

    // update total energy consumption and cpu time
    u32 key0 = 0;
    struct total_consumption *tot = bpf_map_lookup_elem(&total, &key0);
    if (tot) {
        tot->cpu_time += delta_time;
        tot->energy   += delta_energy;
    }

    // update CPU energy snapshot for the next interval
    bpf_map_update_elem(&cpu_to_prev_energy, &cpu, &cur_energy, BPF_ANY);
}


SEC(".struct_ops.link")
struct sched_ext_ops sched_ops = {
    .init      = (void *)sched_init,
    .enqueue   = (void *)sched_enqueue,
    .exit_task      = (void *)sched_exit_task, 
    // .runnable    = (void *)sched_runnable,
    .dispatch  = (void *)sched_dispatch,
    .running   = (void *)sched_running,
    .stopping  = (void *)sched_stopping,
    .flags     = SCX_OPS_ENQ_LAST | SCX_OPS_KEEP_BUILTIN_IDLE,
    .name      = "energy_fair_scheduler",
};

char _license[] SEC("license") = "GPL";