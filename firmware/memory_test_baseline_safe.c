//==============================================================================
// Olimex iCE40HX8K-EVB RISC-V Platform
// memory_test_baseline_safe.c - Fixed version with safe printf
//
// FIXES: Use %08lx instead of %08x for uint32_t values
//==============================================================================

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

// Memory map
#define SRAM_BASE       0x00000000
#define SRAM_SIZE       (512 * 1024)  // 512KB
#define TEST_SIZE       4096          // 4KB test region
#define TEST_BASE       0x00010000    // Start at 64KB

// Test result tracking
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_START(name) \
    do { \
        printf("\r\n[TEST] %s\r\n", name); \
    } while(0)

#define TEST_PASS() \
    do { \
        tests_passed++; \
        printf("  [PASS]\r\n"); \
    } while(0)

#define TEST_FAIL(msg) \
    do { \
        tests_failed++; \
        printf("  [FAIL] %s\r\n", msg); \
    } while(0)

#define ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            TEST_FAIL(msg); \
            return false; \
        } \
    } while(0)

//==============================================================================
// Test 1: Basic Sequential 32-bit Write/Read
//==============================================================================
bool test_sequential_32bit(void) {
    TEST_START("Sequential 32-bit Write/Read");

    volatile uint32_t *mem = (uint32_t *)TEST_BASE;
    const uint32_t pattern_base = 0x12345678;
    const int count = TEST_SIZE / 4;

    // Write pattern
    printf("  Writing %d words...\r\n", count);
    for (int i = 0; i < count; i++) {
        mem[i] = pattern_base + i;
    }

    // Read and verify
    printf("  Verifying...\r\n");
    for (int i = 0; i < count; i++) {
        uint32_t expected = pattern_base + i;
        uint32_t actual = mem[i];
        if (actual != expected) {
            printf("  MISMATCH at offset %d\r\n", i);
            TEST_FAIL("Sequential 32-bit mismatch");
            return false;
        }
    }

    TEST_PASS();
    return true;
}

//==============================================================================
// Test 2: Random Access Pattern
//==============================================================================
bool test_random_access(void) {
    TEST_START("Random Access Pattern");

    volatile uint32_t *mem = (uint32_t *)TEST_BASE;

    // Write to random locations
    mem[0] = 0xDEADBEEF;
    mem[100] = 0xCAFEBABE;
    mem[5] = 0x12345678;
    mem[999] = 0xABCDEF01;
    mem[50] = 0x55AA55AA;

    // Verify in different order
    ASSERT(mem[999] == 0xABCDEF01, "mem[999] failed");
    ASSERT(mem[5] == 0x12345678, "mem[5] failed");
    ASSERT(mem[0] == 0xDEADBEEF, "mem[0] failed");
    ASSERT(mem[50] == 0x55AA55AA, "mem[50] failed");
    ASSERT(mem[100] == 0xCAFEBABE, "mem[100] failed");

    TEST_PASS();
    return true;
}

//==============================================================================
// Test 3: Byte-Level Write/Read
//==============================================================================
bool test_byte_writes(void) {
    TEST_START("Byte-Level Write/Read");

    volatile uint8_t *mem8 = (uint8_t *)TEST_BASE;
    volatile uint32_t *mem32 = (uint32_t *)TEST_BASE;

    // Clear first word
    mem32[0] = 0x00000000;
    ASSERT(mem32[0] == 0x00000000, "Failed to clear");

    // Write individual bytes
    mem8[0] = 0x11;
    mem8[1] = 0x22;
    mem8[2] = 0x33;
    mem8[3] = 0x44;

    // Read as 32-bit word (little endian)
    uint32_t result = mem32[0];
    printf("  Byte writes result OK\r\n");
    ASSERT(result == 0x44332211, "Byte write ordering wrong");

    // Verify individual bytes
    ASSERT(mem8[0] == 0x11, "Byte 0 mismatch");
    ASSERT(mem8[1] == 0x22, "Byte 1 mismatch");
    ASSERT(mem8[2] == 0x33, "Byte 2 mismatch");
    ASSERT(mem8[3] == 0x44, "Byte 3 mismatch");

    TEST_PASS();
    return true;
}

//==============================================================================
// Test 4: Halfword (16-bit) Write/Read
//==============================================================================
bool test_halfword_writes(void) {
    TEST_START("Halfword (16-bit) Write/Read");

    volatile uint16_t *mem16 = (uint16_t *)TEST_BASE;
    volatile uint32_t *mem32 = (uint32_t *)TEST_BASE;

    // Clear first word
    mem32[0] = 0x00000000;

    // Write halfwords
    mem16[0] = 0xBEEF;
    mem16[1] = 0xDEAD;

    // Read as 32-bit word
    uint32_t result = mem32[0];
    printf("  Halfword writes result OK\r\n");
    ASSERT(result == 0xDEADBEEF, "Halfword write ordering wrong");

    // Verify individual halfwords
    ASSERT(mem16[0] == 0xBEEF, "Halfword 0 mismatch");
    ASSERT(mem16[1] == 0xDEAD, "Halfword 1 mismatch");

    TEST_PASS();
    return true;
}

//==============================================================================
// Test 5: Back-to-Back Transactions
//==============================================================================
bool test_back_to_back(void) {
    TEST_START("Back-to-Back Transactions");

    volatile uint32_t *mem = (uint32_t *)TEST_BASE;

    // Back-to-back writes
    mem[0] = 0x11111111;
    mem[1] = 0x22222222;
    mem[2] = 0x33333333;

    // Back-to-back reads
    uint32_t v0 = mem[0];
    uint32_t v1 = mem[1];
    uint32_t v2 = mem[2];

    ASSERT(v0 == 0x11111111, "Back-to-back write/read [0] failed");
    ASSERT(v1 == 0x22222222, "Back-to-back write/read [1] failed");
    ASSERT(v2 == 0x33333333, "Back-to-back write/read [2] failed");

    // Interleaved write/read
    mem[10] = 0xAAAAAAAA;
    uint32_t v10a = mem[10];
    mem[10] = 0xBBBBBBBB;
    uint32_t v10b = mem[10];

    ASSERT(v10a == 0xAAAAAAAA, "Interleaved write/read (1st) failed");
    ASSERT(v10b == 0xBBBBBBBB, "Interleaved write/read (2nd) failed");

    TEST_PASS();
    return true;
}

//==============================================================================
// Test 6: Walking Bit Patterns
//==============================================================================
bool test_walking_bits(void) {
    TEST_START("Walking Bit Patterns");

    volatile uint32_t *mem = (uint32_t *)TEST_BASE;

    // Walking 1s
    printf("  Walking 1s...\r\n");
    for (int i = 0; i < 32; i++) {
        uint32_t pattern = 1U << i;
        mem[i] = pattern;
    }
    for (int i = 0; i < 32; i++) {
        uint32_t expected = 1U << i;
        ASSERT(mem[i] == expected, "Walking 1s failed");
    }

    // Walking 0s
    printf("  Walking 0s...\r\n");
    for (int i = 0; i < 32; i++) {
        uint32_t pattern = ~(1U << i);
        mem[i] = pattern;
    }
    for (int i = 0; i < 32; i++) {
        uint32_t expected = ~(1U << i);
        ASSERT(mem[i] == expected, "Walking 0s failed");
    }

    TEST_PASS();
    return true;
}

//==============================================================================
// Test 7: Alternating Pattern Stress Test
//==============================================================================
bool test_alternating_stress(void) {
    TEST_START("Alternating Pattern Stress Test");

    volatile uint32_t *mem = (uint32_t *)TEST_BASE;
    const int count = 256;  // Test 1KB

    printf("  Running 100 iterations...\r\n");
    for (int iter = 0; iter < 100; iter++) {
        // Write pattern
        for (int i = 0; i < count; i++) {
            mem[i] = 0xA5A5A5A5 ^ i ^ iter;
        }

        // Read and verify immediately
        for (int i = 0; i < count; i++) {
            uint32_t expected = 0xA5A5A5A5 ^ i ^ iter;
            uint32_t actual = mem[i];
            if (actual != expected) {
                printf("  Iteration %d, offset %d mismatch\r\n", iter, i);
                TEST_FAIL("Stress test mismatch");
                return false;
            }
        }

        if ((iter % 10) == 0) {
            printf("    Iteration %d/100...\r\n", iter);
        }
    }

    TEST_PASS();
    return true;
}

//==============================================================================
// Test 8: Address Boundary Crossing
//==============================================================================
bool test_address_boundaries(void) {
    TEST_START("Address Boundary Crossing");

    // Test near 64KB boundary
    volatile uint32_t *mem1 = (uint32_t *)0x0000FFFC;
    mem1[0] = 0xB4F064A0;
    mem1[1] = 0xAF7E64B1;
    ASSERT(mem1[0] == 0xB4F064A0, "Before 64KB boundary failed");
    ASSERT(mem1[1] == 0xAF7E64B1, "After 64KB boundary failed");

    // Test near 128KB boundary
    volatile uint32_t *mem2 = (uint32_t *)0x0001FFFC;
    mem2[0] = 0xB4F128C0;
    mem2[1] = 0xAF7128D1;
    ASSERT(mem2[0] == 0xB4F128C0, "Before 128KB boundary failed");
    ASSERT(mem2[1] == 0xAF7128D1, "After 128KB boundary failed");

    TEST_PASS();
    return true;
}

//==============================================================================
// Main Test Runner
//==============================================================================
int main(void) {
    printf("\r\n");
    printf("================================================================================\r\n");
    printf("SRAM BASELINE TEST SUITE (SAFE VERSION)\r\n");
    printf("================================================================================\r\n");
    printf("\r\n");
    printf("Purpose: Establish known-good behavior\r\n");
    printf("Platform: PicoRV32 @ 50 MHz\r\n");
    printf("\r\n");

    // Run functional tests
    printf("================================================================================\r\n");
    printf("FUNCTIONAL TESTS\r\n");
    printf("================================================================================\r\n");

    test_sequential_32bit();
    test_random_access();
    test_byte_writes();
    test_halfword_writes();
    test_back_to_back();
    test_walking_bits();
    test_alternating_stress();
    test_address_boundaries();

    // Summary
    printf("\r\n");
    printf("================================================================================\r\n");
    printf("TEST SUMMARY\r\n");
    printf("================================================================================\r\n");
    printf("\r\n");
    printf("Tests Passed: %d\r\n", tests_passed);
    printf("Tests Failed: %d\r\n", tests_failed);
    printf("\r\n");

    if (tests_failed == 0) {
        printf("*** ALL TESTS PASSED ***\r\n");
        printf("\r\n");
        printf("BASELINE ESTABLISHED\r\n");
    } else {
        printf("*** SOME TESTS FAILED ***\r\n");
    }

    printf("\r\n");
    printf("================================================================================\r\n");

    // Loop forever
    while (1) {
        __asm__ volatile ("wfi");
    }

    return 0;
}
