#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

/*
COMPILE: gcc -O2 -Wall -o mem_miss mem_miss.c
*/

/*
 * Simple tunable memory stressor to generate cache/TLB misses.
 *
 * Usage:
 *   ./mem_miss [total_mb] [working_set_mb] [accesses] [pattern] [stride_bytes] [write_percent]
 *
 * Parameters (all optional, have defaults):
 *   total_mb       : total allocation size in MB (default 512)
 *   working_set_mb : subset of memory we actually touch in MB (<= total_mb, default 256)
 *   accesses       : number of memory accesses (default 100000000)
 *   pattern        : 0 = sequential/stride, 1 = random (default 1)
 *   stride_bytes   : stride in bytes for pattern=0 (default 4096)
 *   write_percent  : 0–100, probability of doing a write vs read (default 50)
 */

static inline uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + ts.tv_nsec;
}

int main(int argc, char **argv)
{
    size_t total_mb       = (argc > 1) ? strtoull(argv[1], NULL, 10) : 512;
    size_t working_set_mb = (argc > 2) ? strtoull(argv[2], NULL, 10) : 256;
    uint64_t accesses     = (argc > 3) ? strtoull(argv[3], NULL, 10) : 100000000ull;
    int pattern           = (argc > 4) ? atoi(argv[4]) : 1;  // 0=sequential, 1=random
    size_t stride_bytes   = (argc > 5) ? strtoull(argv[5], NULL, 10) : 4096;
    int write_percent     = (argc > 6) ? atoi(argv[6]) : 50;

    if (working_set_mb > total_mb)
        working_set_mb = total_mb;
    if (write_percent < 0) write_percent = 0;
    if (write_percent > 100) write_percent = 100;
    if (stride_bytes == 0) stride_bytes = 64; // at least cacheline

    size_t total_bytes   = total_mb * 1024ull * 1024ull;
    size_t working_bytes = working_set_mb * 1024ull * 1024ull;

    printf("Config:\n");
    printf("  total_mb       = %zu\n", total_mb);
    printf("  working_set_mb = %zu\n", working_set_mb);
    printf("  accesses       = %llu\n", (unsigned long long)accesses);
    printf("  pattern        = %s\n", pattern ? "random" : "sequential/stride");
    printf("  stride_bytes   = %zu\n", stride_bytes);
    printf("  write_percent  = %d\n", write_percent);

    unsigned char *buf = malloc(total_bytes);
    if (!buf) {
        perror("malloc");
        return 1;
    }

    // Touch once to fault in pages initially (optional).
    for (size_t i = 0; i < working_bytes; i += 4096) {
        buf[i] = (unsigned char)(i & 0xFF);
    }

    // Use a volatile sink so the compiler doesn't optimize the loads away.
    volatile unsigned char sink = 0;

    uint64_t seed = (uint64_t)time(NULL);
    uint64_t mask = working_bytes - 1;
    // If working_bytes not power of two, we'll use modulo.

    uint64_t start = now_ns();

    size_t idx = 0;
    for (uint64_t i = 0; i < accesses; i++) {
        if (pattern == 0) {
            // Sequential with stride
            idx += stride_bytes;
            if (idx >= working_bytes)
                idx = 0;
        } else {
            // Simple LCG for reproducible pseudo-random indices
            seed = seed * 6364136223846793005ULL + 1;
            uint64_t r = seed;

            if ((working_bytes & (working_bytes - 1)) == 0) {
                // power of two: faster masking
                idx = (size_t)(r & mask);
            } else {
                idx = (size_t)(r % working_bytes);
            }
        }

        // Optional: align to 64B cache line to make access pattern "clean"
        idx &= ~(size_t)63;

        int do_write = (write_percent == 0) ? 0 :
                       (write_percent == 100) ? 1 :
                       ((seed % 100) < (uint64_t)write_percent);

        if (do_write) {
            buf[idx]++;   // write
        } else {
            sink ^= buf[idx]; // read
        }
    }

    uint64_t end = now_ns();
    double seconds = (end - start) / 1e9;

    printf("Done. Time: %.3f s, accesses: %llu, accesses/s: %.3f M\n",
           seconds,
           (unsigned long long)accesses,
           accesses / (seconds * 1e6));

    // Use sink so compiler can't drop everything.
    printf("Sink = %u\n", sink);

    free((void *)buf);
    return 0;
}
