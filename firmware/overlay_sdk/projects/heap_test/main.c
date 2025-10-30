//===============================================================================
// Overlay Project: heap_test
// Comprehensive Heap Memory Test - Overlay Version
//
// Full port of firmware/heap_test.c adapted for overlay environment
// Tests malloc/free with patterns inspired by memtest86
//
// Adapted for:
// - 24KB overlay heap (vs 248KB firmware heap)
// - Timer interrupt registration at 0x28000
// - Overlay SDK build system (PIC)
// - Clean exit back to SD Card Manager
//
// Copyright (c) October 2025 Michael Wolak
// Email: mikewolak@gmail.com, mike@epromfoundry.com
//===============================================================================

#include "hardware.h"
#include "io.h"
#include "memory_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

//==============================================================================
// Timer Register Definitions
//==============================================================================
#define TIMER_BASE          0x80000020
#define TIMER_CR            (*(volatile uint32_t*)(TIMER_BASE + 0x00))
#define TIMER_SR            (*(volatile uint32_t*)(TIMER_BASE + 0x04))
#define TIMER_PSC           (*(volatile uint32_t*)(TIMER_BASE + 0x08))
#define TIMER_ARR           (*(volatile uint32_t*)(TIMER_BASE + 0x0C))
#define TIMER_CNT           (*(volatile uint32_t*)(TIMER_BASE + 0x10))

// Timer control bits
#define TIMER_CR_ENABLE     (1 << 0)
#define TIMER_SR_UIF        (1 << 0)

//==============================================================================
// Heap Boundaries for Overlay
//==============================================================================
// These are defined in memory_config.h, but we reference them here
// OVERLAY_HEAP_BASE = 0x0007A000
// OVERLAY_HEAP_END  = 0x00080000
// OVERLAY_HEAP_SIZE = 24 KB

// Heap symbols from linker script
extern char __heap_start;
extern char __heap_end;

//==============================================================================
// Throughput Measurement Globals
//==============================================================================
static volatile uint32_t bytes_processed = 0;
static volatile uint32_t seconds_elapsed = 0;
static volatile uint32_t new_second = 0;  // Flag: new second ready to display

//==============================================================================
// Timer Interrupt Handler
//
// Registered at 0x28000 with firmware's IRQ dispatcher
// Called at 1 Hz for throughput measurement
//==============================================================================
void timer_irq_handler(void) {
    // CRITICAL: Clear the interrupt source FIRST
    TIMER_SR = TIMER_SR_UIF;

    // Signal main loop (single store)
    new_second = 1;
}

//==============================================================================
// Timer Helper Functions (heap test specific)
//==============================================================================
static void heap_timer_config(uint16_t psc, uint32_t arr) {
    TIMER_PSC = psc;
    TIMER_ARR = arr;
}

static void heap_timer_start(void) {
    TIMER_CR = TIMER_CR_ENABLE;  // Enable, continuous mode
}

static void heap_timer_stop(void) {
    TIMER_CR = 0;  // Disable timer
}

//==============================================================================
// PicoRV32 IRQ Control (overlay version)
//==============================================================================
static inline void irq_enable(void) {
    uint32_t dummy;
    __asm__ volatile (".insn r 0x0B, 6, 3, %0, %1, x0" : "=r"(dummy) : "r"(0));
}

static inline void irq_disable(void) {
    uint32_t dummy;
    __asm__ volatile (".insn r 0x0B, 6, 3, %0, %1, x0" : "=r"(dummy) : "r"(0xFFFFFFFF));
}

//==============================================================================
// Direct UART getch - no echo, no buffering
//==============================================================================
static int getch(void) {
    while (!uart_getc_available());
    return uart_getc();
}

//==============================================================================
// Memory Test Patterns (from firmware/heap_test.c)
//==============================================================================

static int test_pattern_walking_ones(void *ptr, size_t size) {
    uint32_t *data = (uint32_t*)ptr;
    size_t words = size / sizeof(uint32_t);

    printf("  Walking ones pattern...\r\n");

    // Write walking ones
    for (size_t i = 0; i < words; i++) {
        data[i] = 1U << (i % 32);
    }

    // Verify
    for (size_t i = 0; i < words; i++) {
        if (data[i] != (1U << (i % 32))) {
            printf("  FAIL at offset %u: expected 0x%08lX, got 0x%08lX\r\n",
                   (unsigned int)(i * 4), (unsigned long)(1U << (i % 32)),
                   (unsigned long)data[i]);
            return 0;
        }
    }

    return 1;
}

static int test_pattern_walking_zeros(void *ptr, size_t size) {
    uint32_t *data = (uint32_t*)ptr;
    size_t words = size / sizeof(uint32_t);

    printf("  Walking zeros pattern...\r\n");

    // Write walking zeros
    for (size_t i = 0; i < words; i++) {
        data[i] = ~(1U << (i % 32));
    }

    // Verify
    for (size_t i = 0; i < words; i++) {
        if (data[i] != ~(1U << (i % 32))) {
            printf("  FAIL at offset %u\r\n", (unsigned int)(i * 4));
            return 0;
        }
    }

    return 1;
}

static int test_pattern_checkerboard(void *ptr, size_t size) {
    uint32_t *data = (uint32_t*)ptr;
    size_t words = size / sizeof(uint32_t);

    printf("  Checkerboard pattern...\r\n");

    // Write 0xAAAAAAAA and 0x55555555
    for (size_t i = 0; i < words; i++) {
        data[i] = (i & 1) ? 0x55555555 : 0xAAAAAAAA;
    }

    // Verify
    for (size_t i = 0; i < words; i++) {
        uint32_t expected = (i & 1) ? 0x55555555 : 0xAAAAAAAA;
        if (data[i] != expected) {
            printf("  FAIL at offset %u\r\n", (unsigned int)(i * 4));
            return 0;
        }
    }

    return 1;
}

static int test_pattern_address_in_address(void *ptr, size_t size) {
    uint32_t *data = (uint32_t*)ptr;
    size_t words = size / sizeof(uint32_t);

    printf("  Address-in-address pattern...\r\n");

    // Write address as data
    for (size_t i = 0; i < words; i++) {
        data[i] = (uint32_t)&data[i];
    }

    // Verify
    for (size_t i = 0; i < words; i++) {
        if (data[i] != (uint32_t)&data[i]) {
            printf("  FAIL at offset %u\r\n", (unsigned int)(i * 4));
            return 0;
        }
    }

    return 1;
}

static int test_pattern_random(void *ptr, size_t size) {
    uint32_t *data = (uint32_t*)ptr;
    size_t words = size / sizeof(uint32_t);
    uint32_t seed = 0xDEADBEEF;

    printf("  Random pattern (PRNG)...\r\n");

    // Write pseudo-random data (simple LCG)
    uint32_t rng = seed;
    for (size_t i = 0; i < words; i++) {
        rng = rng * 1664525 + 1013904223;  // LCG parameters
        data[i] = rng;
    }

    // Verify
    rng = seed;
    for (size_t i = 0; i < words; i++) {
        rng = rng * 1664525 + 1013904223;
        if (data[i] != rng) {
            printf("  FAIL at offset %u\r\n", (unsigned int)(i * 4));
            return 0;
        }
    }

    return 1;
}

//==============================================================================
// Test Functions (adapted for 24KB heap)
//==============================================================================

static void test_heap_info(void) {
    uint32_t heap_start = (uint32_t)&__heap_start;
    uint32_t heap_end = (uint32_t)&__heap_end;
    uint32_t heap_size = heap_end - heap_start;

    printf("\r\n");
    printf("=== Overlay Heap Information ===\r\n");
    printf("Heap start:     0x%08lX\r\n", (unsigned long)heap_start);
    printf("Heap end:       0x%08lX\r\n", (unsigned long)heap_end);
    printf("Heap size:      %lu bytes (%lu KB)\r\n",
           (unsigned long)heap_size, (unsigned long)(heap_size / 1024));
    printf("Expected:       0x%08X - 0x%08X (24 KB)\r\n",
           OVERLAY_HEAP_BASE, OVERLAY_HEAP_END);
}

static void test_single_allocation(void) {
    printf("\r\n");
    printf("=== Single Allocation Test ===\r\n");

    // Scaled down for 24KB heap (firmware used 16-16384 bytes)
    size_t sizes[] = {16, 64, 256, 1024, 4096, 8192};

    for (size_t i = 0; i < sizeof(sizes)/sizeof(sizes[0]); i++) {
        printf("Allocating %lu bytes... ", (unsigned long)sizes[i]);
        fflush(stdout);

        void *ptr = malloc(sizes[i]);
        if (!ptr) {
            printf("FAIL (malloc returned NULL)\r\n");
            continue;
        }

        // Write and verify
        memset(ptr, 0xAA, sizes[i]);
        int ok = 1;
        for (size_t j = 0; j < sizes[i]; j++) {
            if (((uint8_t*)ptr)[j] != 0xAA) {
                ok = 0;
                break;
            }
        }

        free(ptr);
        printf("%s\r\n", ok ? "PASS" : "FAIL");
    }
}

static void test_multiple_allocations(void) {
    printf("\r\n");
    printf("=== Multiple Allocations Test ===\r\n");

    // Scaled down: 10 x 512B blocks = 5KB (firmware used 10 x 1KB)
    #define NUM_ALLOCS 10
    void *ptrs[NUM_ALLOCS];

    printf("Allocating %d blocks of 512B each...\r\n", NUM_ALLOCS);

    for (int i = 0; i < NUM_ALLOCS; i++) {
        ptrs[i] = malloc(512);
        if (!ptrs[i]) {
            printf("FAIL: malloc returned NULL at block %d\r\n", i);
            for (int j = 0; j < i; j++) free(ptrs[j]);
            return;
        }
        memset(ptrs[i], i & 0xFF, 512);
    }

    printf("Verifying data...\r\n");
    int ok = 1;
    for (int i = 0; i < NUM_ALLOCS; i++) {
        for (int j = 0; j < 512; j++) {
            if (((uint8_t*)ptrs[i])[j] != (uint8_t)(i & 0xFF)) {
                printf("FAIL: corruption in block %d\r\n", i);
                ok = 0;
                break;
            }
        }
    }

    printf("Freeing all blocks...\r\n");
    for (int i = 0; i < NUM_ALLOCS; i++) {
        free(ptrs[i]);
    }

    printf("%s\r\n", ok ? "PASS" : "FAIL");
}

static void test_fragmentation(void) {
    printf("\r\n");
    printf("=== Fragmentation Test ===\r\n");

    // Scaled down: 20 x 256B blocks = 5KB (firmware used 20 x 512B)
    #define FRAG_ALLOCS 20
    void *ptrs[FRAG_ALLOCS];

    printf("Allocating %d blocks (256B each)...\r\n", FRAG_ALLOCS);
    for (int i = 0; i < FRAG_ALLOCS; i++) {
        ptrs[i] = malloc(256);
        if (!ptrs[i]) {
            printf("FAIL: malloc at block %d\r\n", i);
            for (int j = 0; j < i; j++) if (ptrs[j]) free(ptrs[j]);
            return;
        }
    }

    printf("Freeing every other block...\r\n");
    for (int i = 0; i < FRAG_ALLOCS; i += 2) {
        free(ptrs[i]);
        ptrs[i] = NULL;
    }

    printf("Re-allocating freed blocks...\r\n");
    for (int i = 0; i < FRAG_ALLOCS; i += 2) {
        ptrs[i] = malloc(256);
        if (!ptrs[i]) {
            printf("FAIL: re-malloc at block %d\r\n", i);
            for (int j = 0; j < FRAG_ALLOCS; j++) if (ptrs[j]) free(ptrs[j]);
            return;
        }
    }

    printf("Freeing all blocks...\r\n");
    for (int i = 0; i < FRAG_ALLOCS; i++) {
        if (ptrs[i]) free(ptrs[i]);
    }

    printf("PASS\r\n");
}

static void test_memory_patterns(void) {
    printf("\r\n");
    printf("=== Memory Pattern Test ===\r\n");

    // Calculate available heap space
    uint32_t heap_start = (uint32_t)&__heap_start;
    uint32_t heap_end = (uint32_t)&__heap_end;
    uint32_t heap_total = heap_end - heap_start;

    printf("Total heap space: %lu bytes (%lu KB)\r\n",
           (unsigned long)heap_total, (unsigned long)(heap_total / 1024));

    // Try to allocate maximum available heap
    // Start with 90% of total, reduce if fails
    size_t test_size = (heap_total * 9) / 10;
    void *ptr = NULL;

    printf("Attempting to allocate maximum available heap...\r\n");
    fflush(stdout);

    while (test_size > 4096 && !ptr) {
        ptr = malloc(test_size);
        if (!ptr) {
            test_size = (test_size * 9) / 10;  // Reduce by 10%
        }
    }

    if (!ptr) {
        printf("FAIL: Unable to allocate even 4KB of heap\r\n");
        return;
    }

    printf("Allocated %lu bytes (%lu KB, %.1f%% of heap)\r\n",
           (unsigned long)test_size,
           (unsigned long)(test_size / 1024),
           (float)test_size * 100.0 / heap_total);
    printf("Testing entire allocated region with 5 patterns...\r\n");
    fflush(stdout);

    int all_pass = 1;
    all_pass &= test_pattern_walking_ones(ptr, test_size);
    all_pass &= test_pattern_walking_zeros(ptr, test_size);
    all_pass &= test_pattern_checkerboard(ptr, test_size);
    all_pass &= test_pattern_address_in_address(ptr, test_size);
    all_pass &= test_pattern_random(ptr, test_size);

    free(ptr);
    printf("\r\n");
    printf("%s\r\n", all_pass ? "ALL PATTERNS PASS" : "SOME PATTERNS FAILED");
}

static void test_stress_allocations(void) {
    printf("\r\n");
    printf("=== Stress Test (10 seconds) ===\r\n");
    printf("Rapid malloc/free cycles with verification...\r\n");
    printf("This will take ~10 seconds...\r\n");
    fflush(stdout);

    // Scaled down: 5000 iterations (firmware used 10000)
    // Smaller size range for 24KB heap
    uint32_t iterations = 5000;
    uint32_t seed = 0x12345678;
    int failures = 0;

    for (uint32_t i = 0; i < iterations; i++) {
        // Pseudo-random size (50 - 500 bytes) - scaled down from 100-2000
        seed = seed * 1664525 + 1013904223;
        size_t size = 50 + (seed % 450);

        void *ptr = malloc(size);
        if (!ptr) {
            failures++;
            continue;
        }

        // Fill with pattern
        uint8_t pattern = (uint8_t)(seed & 0xFF);
        memset(ptr, pattern, size);

        // Verify
        for (size_t j = 0; j < size; j++) {
            if (((uint8_t*)ptr)[j] != pattern) {
                failures++;
                break;
            }
        }

        free(ptr);

        // Progress indicator every 500 iterations
        if ((i + 1) % 500 == 0) {
            printf("  %lu iterations complete...\r\n", (unsigned long)(i + 1));
            fflush(stdout);
        }
    }

    printf("\r\n");
    printf("Completed %lu iterations\r\n", (unsigned long)iterations);
    printf("Failures: %d\r\n", failures);
    printf("%s\r\n", failures == 0 ? "PASS" : "FAIL");
}

// Helper function to run a throughput test pattern
static void run_pattern_test(const char *pattern_name,
                              void *src, void *dst, size_t buf_size,
                              int is_read_test, int access_width) {
    printf("\r\n--- %s: %s (10 seconds) ---\r\n",
           is_read_test ? "READ" : "WRITE", pattern_name);
    fflush(stdout);

    bytes_processed = 0;
    seconds_elapsed = 0;
    new_second = 0;
    uint32_t last_bytes = 0;

    // Enable timer
    heap_timer_start();

    // Run test for 10 seconds or until keypress
    volatile uint32_t dummy = 0;
    int exit_requested = 0;

    while (seconds_elapsed < 10 && !exit_requested) {
        if (is_read_test) {
            // READ test - read from memory
            if (access_width == 1) {
                uint8_t *ptr = (uint8_t*)src;
                for (size_t i = 0; i < buf_size; i++) {
                    dummy += ptr[i];
                    if ((i & 0x3FF) == 0 && (UART_RX_STATUS & 0x01)) {
                        exit_requested = 1;
                        break;
                    }
                }
            } else if (access_width == 2) {
                uint16_t *ptr = (uint16_t*)src;
                size_t halfwords = buf_size / 2;
                for (size_t i = 0; i < halfwords; i++) {
                    dummy += ptr[i];
                    if ((i & 0x3FF) == 0 && (UART_RX_STATUS & 0x01)) {
                        exit_requested = 1;
                        break;
                    }
                }
            } else if (access_width == 4) {
                uint32_t *ptr = (uint32_t*)src;
                size_t words = buf_size / 4;
                for (size_t i = 0; i < words; i++) {
                    dummy += ptr[i];
                    if ((i & 0x3FF) == 0 && (UART_RX_STATUS & 0x01)) {
                        exit_requested = 1;
                        break;
                    }
                }
            } else {
                // memcpy (copy operation)
                memcpy(dst, src, buf_size);
            }
        } else {
            // WRITE test - write to memory
            if (access_width == 1) {
                uint8_t *ptr = (uint8_t*)dst;
                for (size_t i = 0; i < buf_size; i++) {
                    ptr[i] = 0xAA;
                    if ((i & 0x3FF) == 0 && (UART_RX_STATUS & 0x01)) {
                        exit_requested = 1;
                        break;
                    }
                }
            } else if (access_width == 2) {
                uint16_t *ptr = (uint16_t*)dst;
                size_t halfwords = buf_size / 2;
                for (size_t i = 0; i < halfwords; i++) {
                    ptr[i] = 0xAAAA;
                    if ((i & 0x3FF) == 0 && (UART_RX_STATUS & 0x01)) {
                        exit_requested = 1;
                        break;
                    }
                }
            } else if (access_width == 4) {
                uint32_t *ptr = (uint32_t*)dst;
                size_t words = buf_size / 4;
                for (size_t i = 0; i < words; i++) {
                    ptr[i] = 0xAAAAAAAA;
                    if ((i & 0x3FF) == 0 && (UART_RX_STATUS & 0x01)) {
                        exit_requested = 1;
                        break;
                    }
                }
            } else {
                // memcpy (copy operation)
                memcpy(dst, src, buf_size);
            }
        }

        bytes_processed += buf_size;

        // Check for new second
        if (new_second) {
            new_second = 0;
            seconds_elapsed++;

            uint32_t bytes_this_sec = bytes_processed - last_bytes;
            last_bytes = bytes_processed;

            if (bytes_this_sec >= 1000000) {
                printf("  [%2lus] %lu.%02lu MB/s\r\n",
                       (unsigned long)seconds_elapsed,
                       (unsigned long)(bytes_this_sec / 1000000),
                       (unsigned long)((bytes_this_sec % 1000000) / 10000));
            } else {
                printf("  [%2lus] %lu.%02lu KB/s\r\n",
                       (unsigned long)seconds_elapsed,
                       (unsigned long)(bytes_this_sec / 1000),
                       (unsigned long)((bytes_this_sec % 1000) / 10));
            }
            fflush(stdout);
        }
    }

    // Stop timer
    heap_timer_stop();

    // Calculate average
    if (seconds_elapsed > 0) {
        uint32_t avg = bytes_processed / seconds_elapsed;
        printf("  Average: %lu.%02lu MB/s\r\n",
               (unsigned long)(avg / 1000000),
               (unsigned long)((avg % 1000000) / 10000));
    }

    (void)dummy;  // Prevent optimization
}

static void test_throughput(void) {
    printf("\r\n");
    printf("=== Memory Throughput Test ===\r\n");
    printf("Tests READ and WRITE with different access widths\r\n");
    printf("Each pattern runs for 10 seconds\r\n");
    printf("Press 's' to start, 'q' to quit\r\n");
    fflush(stdout);

    // Wait for 's' to start
    while (1) {
        int ch = getch();
        if (ch == 's' || ch == 'S') break;
        if (ch == 'q' || ch == 'Q') return;
    }

    printf("\r\nStarting throughput benchmark...\r\n");
    printf("Press any key to skip current test\r\n");
    fflush(stdout);

    // Allocate test buffers - scaled down to 8KB each for 24KB heap (firmware used 64KB)
    // 2 x 8KB = 16KB fits in 24KB heap with room for malloc overhead
    const size_t buf_size = 8192;
    void *src = malloc(buf_size);
    void *dst = malloc(buf_size);

    if (!src || !dst) {
        printf("FAIL: malloc failed\r\n");
        free(src);
        free(dst);
        return;
    }

    // Fill source with pattern
    memset(src, 0xAA, buf_size);

    // CRITICAL: Register our timer interrupt handler with the firmware
    printf("Registering timer IRQ handler at 0x28000...\r\n");
    void (**overlay_timer_irq_handler_ptr)(void) = (void (**)(void))0x28000;
    *overlay_timer_irq_handler_ptr = timer_irq_handler;

    // Setup timer for 1 Hz interrupts
    heap_timer_config(49, 999999);

    // Enable interrupts
    irq_enable();

    // Run all test patterns
    printf("\r\n========== READ TESTS ==========\r\n");
    run_pattern_test("memcpy (copy)", src, dst, buf_size, 1, 0);
    run_pattern_test("8-bit reads", src, dst, buf_size, 1, 1);
    run_pattern_test("16-bit reads", src, dst, buf_size, 1, 2);
    run_pattern_test("32-bit reads", src, dst, buf_size, 1, 4);

    printf("\r\n========== WRITE TESTS ==========\r\n");
    run_pattern_test("memcpy (copy)", src, dst, buf_size, 0, 0);
    run_pattern_test("8-bit writes", src, dst, buf_size, 0, 1);
    run_pattern_test("16-bit writes", src, dst, buf_size, 0, 2);
    run_pattern_test("32-bit writes", src, dst, buf_size, 0, 4);

    printf("\r\n========================================\r\n");
    printf("Throughput benchmark complete!\r\n");
    printf("========================================\r\n");

    // Simple clean shutdown - stop timer and disable interrupts
    heap_timer_stop();
    irq_disable();

    // Unregister our timer interrupt handler
    printf("Unregistering timer IRQ handler...\r\n");
    *overlay_timer_irq_handler_ptr = 0;

    // Drain UART buffer of any keypresses during tests
    while (UART_RX_STATUS & 0x01) {
        (void)uart_getc();
    }

    // Reset global state
    new_second = 0;
    bytes_processed = 0;
    seconds_elapsed = 0;

    free(src);
    free(dst);
}

//==============================================================================
// Main Menu
//==============================================================================

static void show_menu(void) {
    printf("\r\n");
    printf("========================================\r\n");
    printf("  Heap Memory Test Suite (OVERLAY)\r\n");
    printf("========================================\r\n");
    printf("1. Heap information\r\n");
    printf("2. Single allocation test\r\n");
    printf("3. Multiple allocations test\r\n");
    printf("4. Fragmentation test\r\n");
    printf("5. Memory pattern test\r\n");
    printf("6. Stress test (10 seconds)\r\n");
    printf("7. Throughput test (real-time)\r\n");
    printf("8. Run all tests\r\n");
    printf("h. Show this menu\r\n");
    printf("q. Quit (return to SD Card Manager)\r\n");
    printf("========================================\r\n");
    printf("Select option: ");
    fflush(stdout);
}

//==============================================================================
// Main Entry Point
//==============================================================================

int main(void) {
    printf("\r\n\r\n");
    printf("========================================\r\n");
    printf("  Heap Memory Test Suite (OVERLAY)\r\n");
    printf("  malloc/free stress testing\r\n");
    printf("  24 KB Overlay Heap\r\n");
    printf("========================================\r\n");
    printf("\r\n");
    printf("Press any key to start...\r\n");

    getch();

    printf("\r\n");
    printf("Terminal connected!\r\n");

    show_menu();

    while (1) {
        int choice = getch();

        printf("\r\n");

        switch (choice) {
            case '1':
                test_heap_info();
                show_menu();
                break;

            case '2':
                test_single_allocation();
                show_menu();
                break;

            case '3':
                test_multiple_allocations();
                show_menu();
                break;

            case '4':
                test_fragmentation();
                show_menu();
                break;

            case '5':
                test_memory_patterns();
                show_menu();
                break;

            case '6':
                test_stress_allocations();
                show_menu();
                break;

            case '7':
                test_throughput();
                show_menu();
                break;

            case '8':
                test_heap_info();
                test_single_allocation();
                test_multiple_allocations();
                test_fragmentation();
                test_memory_patterns();
                test_stress_allocations();
                test_throughput();
                printf("\r\n");
                printf("========================================\r\n");
                printf("All heap tests complete!\r\n");
                printf("========================================\r\n");
                show_menu();
                break;

            case 'h':
            case 'H':
                show_menu();
                break;

            case 'q':
            case 'Q':
                printf("Quitting...\r\n");
                printf("Returning to SD Card Manager...\r\n");
                // Clean return to SD Card Manager
                return 0;

            default:
                printf("Invalid option: '%c'. Press 'h' for menu.\r\n", choice);
                break;
        }
    }

    return 0;
}
