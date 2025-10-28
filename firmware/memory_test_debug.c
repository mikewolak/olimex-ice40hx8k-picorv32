//==============================================================================
// Debug version - runs tests one by one to find which locks up
//==============================================================================

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#define TEST_SIZE       4096
#define TEST_BASE       0x00010000

int main(void) {
    printf("\r\n");
    printf("Memory Test - Debug Version\r\n");
    printf("Testing each component separately...\r\n");
    printf("\r\n");

    // Test 1: Just printf
    printf("Test 1: Printf... ");
    printf("OK\r\n");

    // Test 2: Local variable
    printf("Test 2: Local variable... ");
    uint32_t local_var = 0x12345678;
    printf("OK (value=0x%08lx)\r\n", (unsigned long)local_var);

    // Test 3: Pointer to TEST_BASE (just create pointer, don't access)
    printf("Test 3: Create pointer to TEST_BASE... ");
    volatile uint32_t *mem = (uint32_t *)TEST_BASE;
    printf("OK (ptr=0x%08lx)\r\n", (unsigned long)mem);

    // Test 4: Single write
    printf("Test 4: Single write to TEST_BASE... ");
    mem[0] = 0xDEADBEEF;
    printf("OK\r\n");

    // Test 5: Single read
    printf("Test 5: Single read from TEST_BASE... ");
    uint32_t val = mem[0];
    printf("OK (val=0x%08lx)\r\n", (unsigned long)val);

    // Test 6: Simple loop (10 writes)
    printf("Test 6: Loop 10 writes... ");
    for (int i = 0; i < 10; i++) {
        mem[i] = 0x11110000 + i;
    }
    printf("OK\r\n");

    // Test 7: Simple loop (10 reads)
    printf("Test 7: Loop 10 reads... ");
    for (int i = 0; i < 10; i++) {
        val = mem[i];
    }
    printf("OK\r\n");

    // Test 8: Larger loop (100 writes)
    printf("Test 8: Loop 100 writes... ");
    for (int i = 0; i < 100; i++) {
        mem[i] = 0x22220000 + i;
    }
    printf("OK\r\n");

    // Test 9: Larger loop (100 reads)
    printf("Test 9: Loop 100 reads... ");
    for (int i = 0; i < 100; i++) {
        val = mem[i];
    }
    printf("OK\r\n");

    // Test 10: Full 4KB (1024 words)
    printf("Test 10: Full 4KB writes... ");
    for (int i = 0; i < 1024; i++) {
        mem[i] = 0x33330000 + i;
    }
    printf("OK\r\n");

    printf("\r\n");
    printf("ALL DEBUG TESTS PASSED!\r\n");
    printf("\r\n");

    while (1) {
        __asm__ volatile ("wfi");
    }

    return 0;
}
