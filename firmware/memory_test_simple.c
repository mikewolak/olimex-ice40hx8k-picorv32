//==============================================================================
// Simple Memory Test - Just Printf, No Memory Access
//==============================================================================

#include <stdio.h>
#include <stdint.h>

int main(void) {
    printf("\r\n");
    printf("========================================\r\n");
    printf("Simple Memory Test\r\n");
    printf("========================================\r\n");
    printf("\r\n");

    // Test 1: Just printf
    printf("Test 1: Printf works!\r\n");

    // Test 2: Simple variable
    int x = 42;
    printf("Test 2: x = %d\r\n", x);

    // Test 3: Pointer to stack variable
    int *ptr = &x;
    printf("Test 3: ptr = 0x%08lx, *ptr = %d\r\n", (unsigned long)ptr, *ptr);

    // Test 4: Safe heap-like memory (well past code)
    volatile uint32_t *test_mem = (uint32_t *)0x00020000;  // 128KB offset
    test_mem[0] = 0xDEADBEEF;
    uint32_t read_val = test_mem[0];
    printf("Test 4: Wrote 0xDEADBEEF, read 0x%08lx\r\n", (unsigned long)read_val);

    if (read_val == 0xDEADBEEF) {
        printf("\r\nSUCCESS: All tests passed!\r\n");
    } else {
        printf("\r\nFAILED: Memory test failed!\r\n");
    }

    printf("\r\nDone. Looping forever...\r\n");

    // Loop forever
    while (1) {
        __asm__ volatile ("wfi");
    }

    return 0;
}
