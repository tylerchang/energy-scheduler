#include <bpf/libbpf.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main() {
    struct bpf_object *obj;
    int prog_fd, map_fd;
    int err;

    // Open BPF object file
    obj = bpf_object__open_file("test.bpf.o", NULL);
    if (libbpf_get_error(obj)) {
        fprintf(stderr, "Failed to open BPF object\n");
        return 1;
    }

    // Load (verify) the BPF program
    err = bpf_object__load(obj);
    if (err) {
        fprintf(stderr, "Failed to load BPF object: %d\n", err);
        return 1;
    }

    // Find your program section by name
    struct bpf_program *prog = bpf_object__find_program_by_name(obj, "handle_sched_switch");
    if (!prog) {
        fprintf(stderr, "Failed to find program section\n");
        return 1;
    }

    // Attach to tracepoint "sched:sched_switch"
    struct bpf_link *link = bpf_program__attach_tracepoint(prog, "sched", "sched_switch");
    if (libbpf_get_error(link)) {
        fprintf(stderr, "Failed to attach to tracepoint\n");
        return 1;
    }

    printf("BPF program loaded and attached! (press Ctrl+C to stop)\n");

    // Initialize the map
    map_fd = bpf_object__find_map_fd_by_name(obj, "limit");
    if (map_fd < 0) {
        fprintf(stderr, "Failed to find map\n");
        return 1;
    }

    // Keep the program running
    while (1) {
        sleep(1);
    }

    bpf_link__destroy(link);
    bpf_object__close(obj);
    return 0;
}
