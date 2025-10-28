//==============================================================================
// Olimex iCE40HX8K-EVB RISC-V Platform
// memory_test_baseline.c - Comprehensive SRAM Baseline Test Suite
//
// PURPOSE: Establish known-good behavior before SRAM optimization
// CRITICAL: This must pass 100% before any hardware changes
//
// Copyright (c) October 2025 Michael Wolak
//==============================================================================

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

// Memory map
#define SRAM_BASE       0x00000000
#define SRAM_SIZE       (512 * 1024)  // 512KB
#define TEST_SIZE       4096          // 4KB test region (safe, doesn't overwrite code)
#define TEST_BASE       0x00010000    // Start at 64KB (well past code)

// UART registers
#define UART_BASE       0x80000000
#define UART_DATA       (*(volatile uint32_t *)(UART_BASE + 0x00))
#define UART_STATUS     (*(volatile uint32_t *)(UART_BASE + 0x04))

// NOTE: PicoRV32 has ENABLE_COUNTERS=0, so rdcycle doesn't work
// We'll skip cycle counting for now (can add timer-based counting later)
static inline uint32_t get_cycles(void) {
    // Return 0 for now - benchmarks will show 0 cycles but functional tests still work
    return 0;
}

// Test result tracking
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_START(name) \
    do { \
        printf("\n[TEST] %s\n", name); \
    } while(0)

#define TEST_PASS() \
    do { \
        tests_passed++; \
        printf("  [PASS]\n"); \
    } while(0)

#define TEST_FAIL(msg) \
    do { \
        tests_failed++; \
        printf("  [FAIL] %s\n", msg); \
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
    printf("  Writing %d words...\n", count);
    for (int i = 0; i < count; i++) {
        mem[i] = pattern_base + i;
    }

    // Read and verify
    printf("  Verifying...\n");
    for (int i = 0; i < count; i++) {
        uint32_t expected = pattern_base + i;
        uint32_t actual = mem[i];
        if (actual != expected) {
            printf("  MISMATCH at offset %d: expected 0x%08x, got 0x%08x\n",
                   i, expected, actual);
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

    // Read as 32-bit word (little endian: LSB at lowest address)
    uint32_t result = mem32[0];
    printf("  Byte writes: 0x11 0x22 0x33 0x44 -> word: 0x%08x\n", result);
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
    printf("  Halfword writes: 0xBEEF 0xDEAD -> word: 0x%08x\n", result);
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
    printf("  Walking 1s...\n");
    for (int i = 0; i < 32; i++) {
        uint32_t pattern = 1U << i;
        mem[i] = pattern;
    }
    for (int i = 0; i < 32; i++) {
        uint32_t expected = 1U << i;
        ASSERT(mem[i] == expected, "Walking 1s failed");
    }

    // Walking 0s
    printf("  Walking 0s...\n");
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

    printf("  Running 100 iterations...\n");
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
                printf("  Iteration %d, offset %d: expected 0x%08x, got 0x%08x\n",
                       iter, i, expected, actual);
                TEST_FAIL("Stress test mismatch");
                return false;
            }
        }

        if ((iter % 10) == 0) {
            printf("    Iteration %d/100...\n", iter);
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
// Test 9: Read Performance Benchmark
//==============================================================================
void benchmark_sequential_read(void) {
    TEST_START("Sequential Read Benchmark");

    volatile uint32_t *mem = (uint32_t *)TEST_BASE;
    const int count = 1000;

    // Initialize memory
    for (int i = 0; i < count; i++) {
        mem[i] = i;
    }

    // Benchmark
    uint32_t start = get_cycles();
    uint32_t sum = 0;
    for (int i = 0; i < count; i++) {
        sum += mem[i];
    }
    uint32_t end = get_cycles();

    uint32_t total_cycles = end - start;
    uint32_t cycles_per_read = total_cycles / count;

    printf("  Total cycles: %u\n", total_cycles);
    printf("  Cycles per read: %u\n", cycles_per_read);
    printf("  Checksum: 0x%08x (prevents optimization)\n", sum);

    TEST_PASS();
}

//==============================================================================
// Test 10: Write Performance Benchmark
//==============================================================================
void benchmark_sequential_write(void) {
    TEST_START("Sequential Write Benchmark");

    volatile uint32_t *mem = (uint32_t *)TEST_BASE;
    const int count = 1000;

    uint32_t start = get_cycles();
    for (int i = 0; i < count; i++) {
        mem[i] = i;
    }
    uint32_t end = get_cycles();

    uint32_t total_cycles = end - start;
    uint32_t cycles_per_write = total_cycles / count;

    printf("  Total cycles: %u\n", total_cycles);
    printf("  Cycles per write: %u\n", cycles_per_write);

    TEST_PASS();
}

//==============================================================================
// Main Test Runner
//==============================================================================
int main(void) {
    printf("\n");
    printf("================================================================================\n");
    printf("SRAM BASELINE TEST SUITE\n");
    printf("================================================================================\n");
    printf("\n");
    printf("Purpose: Establish known-good behavior before SRAM optimization\n");
    printf("Platform: Olimex iCE40HX8K-EVB, PicoRV32 @ 50 MHz\n");
    printf("SRAM: IS61WV51216BLL-10TLI (512KB, 10ns access)\n");
    printf("\n");
    printf("Test region: 0x%08x - 0x%08x (%d bytes)\n",
           TEST_BASE, TEST_BASE + TEST_SIZE, TEST_SIZE);
    printf("\n");

    // Run functional tests
    printf("================================================================================\n");
    printf("FUNCTIONAL TESTS\n");
    printf("================================================================================\n");

    test_sequential_32bit();
    test_random_access();
    test_byte_writes();
    test_halfword_writes();
    test_back_to_back();
    test_walking_bits();
    test_alternating_stress();
    test_address_boundaries();

    // Run benchmarks
    printf("\n");
    printf("================================================================================\n");
    printf("PERFORMANCE BENCHMARKS\n");
    printf("================================================================================\n");

    benchmark_sequential_read();
    benchmark_sequential_write();

    // Summary
    printf("\n");
    printf("================================================================================\n");
    printf("TEST SUMMARY\n");
    printf("================================================================================\n");
    printf("\n");
    printf("Tests Passed: %d\n", tests_passed);
    printf("Tests Failed: %d\n", tests_failed);
    printf("\n");

    if (tests_failed == 0) {
        printf("*** ALL TESTS PASSED ***\n");
        printf("\n");
        printf("BASELINE ESTABLISHED - Safe to proceed with optimization\n");
    } else {
        printf("*** SOME TESTS FAILED ***\n");
        printf("\n");
        printf("DO NOT PROCEED with optimization until all tests pass!\n");
    }

    printf("\n");
    printf("================================================================================\n");

    // Loop forever
    while (1) {
        __asm__ volatile ("wfi");
    }

    return 0;
}
