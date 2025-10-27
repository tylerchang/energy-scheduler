#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

// Define a shared Dispatch Queue (DSQ) ID
#define SHARED_DSQ_ID 0
#ifndef PF_KTHREAD
#define PF_KTHREAD 0x00200000
#endif

#define BPF_STRUCT_OPS(name, args...)	\
    SEC("struct_ops/"#name)	BPF_PROG(name, ##args)

#define BPF_STRUCT_OPS_SLEEPABLE(name, args...)	\
    SEC("struct_ops.s/"#name)							      \
    BPF_PROG(name, ##args)

// We use the new names from 6.13 to make it more readable
#define scx_bpf_dsq_insert_vtime scx_bpf_dispatch_vtime
#define scx_bpf_dsq_move scx_bpf_dispatch_from_dsq
#define scx_bpf_dsq_move_vtime scx_bpf_dispatch_vtime_from_dsq

// Initialize the scheduler by creating a shared dispatch queue (DSQ)
s32 BPF_STRUCT_OPS_SLEEPABLE(sched_init) {
    return scx_bpf_create_dsq(SHARED_DSQ_ID, -1);
}

// Enqueue a task to the shared DSQ, dispatching it with a time slice
int BPF_STRUCT_OPS(sched_enqueue, struct task_struct *p, u64 enq_flags) {
    // Calculate the time slice for the task based on the number of tasks in the queue
    // u64 slice = 5000000u / scx_bpf_dsq_nr_queued(SHARED_DSQ_ID);
    u64 slice = 5000000u;
    if (p->flags & PF_KTHREAD) {
        // Kernel threads go to the global DSQ (can run on any CPU)
        scx_bpf_dsq_insert(p, SCX_DSQ_GLOBAL, slice, enq_flags);
    }
    else {
        u32 cpu = bpf_get_prandom_u32() % 2;
        scx_bpf_dsq_insert(p, SCX_DSQ_LOCAL_ON | cpu, slice, enq_flags);
    }
    
    return 0;
}

// Define the main scheduler operations structure (sched_ops)
SEC(".struct_ops.link")
struct sched_ext_ops sched_ops = {
    .enqueue   = (void *)sched_enqueue,
    .init      = (void *)sched_init,
    .flags     = SCX_OPS_ENQ_LAST | SCX_OPS_KEEP_BUILTIN_IDLE,
    .name      = "minimal_scheduler"
};

// License for the BPF program
char _license[] SEC("license") = "GPL";
