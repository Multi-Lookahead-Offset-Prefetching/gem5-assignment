/* Small, reproducible L1-data-miss workload for a gem5 smoke test. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define BYTES (512 * 1024)
#define STRIDE 64

static uint8_t data[BYTES] __attribute__((aligned(64)));

int
main(int argc, char **argv)
{
    volatile uint8_t *const blocks = data;
    uint64_t total = 0;
    const char *mode = argc > 1 ? argv[1] : "sequential";

    if (strcmp(mode, "sequential") == 0) {
        for (int pass = 0; pass < 2; ++pass) {
            for (unsigned i = 0; i < BYTES; i += STRIDE) {
                total += blocks[i];
            }
        }
    } else if (strcmp(mode, "backward") == 0) {
        for (int pass = 0; pass < 2; ++pass) {
            for (int i = BYTES - STRIDE; i >= 0; i -= STRIDE) {
                total += blocks[i];
            }
        }
    } else if (strcmp(mode, "mixed") == 0) {
        for (int pass = 0; pass < 2; ++pass) {
            for (unsigned i = 0; i < BYTES / 4; i += STRIDE) {
                total += blocks[i];
                total += blocks[BYTES / 2 + 2 * i];
            }
        }
    } else {
        fprintf(stderr, "Unknown access pattern: %s\n", mode);
        return 1;
    }

    printf("checksum=%llu\n", (unsigned long long)total);
    return 0;
}
