// All linux kernel type definitions are in vmlinux.h
#include "vmlinux.h"
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, u32); 
    __type(value, u32);
} limit SEC(".maps");

char LICENSE[] SEC("license") = "Dual BSD/GPL";

// extern int read_core_energy(int cpu);
extern int read_core_energy(int cpu) __ksym;


SEC("tracepoint/sched/sched_switch")
int handle_sched_switch (struct trace_event_raw_sched_switch *ctx) {
    
    u32 key = 0;
    u32 *limit_num = bpf_map_lookup_elem(&limit, &key);

    if (*limit_num < 0) {
        bpf_map_update_elem(&limit, &key, 0, BPF_ANY);
    }

    if (*limit_num < 10) {
        read_core_energy(1);
        (*limit_num)++;
        bpf_map_update_elem(&limit, &key, limit_num, BPF_ANY);
    }

	return 0;
}

