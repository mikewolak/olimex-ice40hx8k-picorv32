//==============================================================================
// Overlay Project: heap_test
// main.c - Heap allocation and CRC32 verification test
//
// Tests:
// - Malloc 32KB block #1
// - Fill with random data
// - Calculate CRC32
// - Malloc 32KB block #2
// - memcpy from block #1 to block #2
// - Calculate CRC32 of block #2
// - Compare CRCs
// - Free both blocks
//
// Copyright (c) October 2025
//==============================================================================

#include "hardware.h"
#include "io.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

//==============================================================================
// CRC32 Implementation (matches overlay_upload.c)
//==============================================================================

static uint32_t crc32_table[256];
static uint8_t crc32_initialized = 0;

static void crc32_init(void) {
    if (crc32_initialized) return;

    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320 : 0);
        }
        crc32_table[i] = crc;
    }
    crc32_initialized = 1;
}

static uint32_t calculate_crc32(uint8_t *data, uint32_t length) {
    uint32_t crc = 0xFFFFFFFF;
    for (uint32_t i = 0; i < length; i++) {
        crc = (crc >> 8) ^ crc32_table[(crc ^ data[i]) & 0xFF];
    }
    return ~crc;
}

//==============================================================================
// Simple PRNG for random data generation
//==============================================================================

static uint32_t rand_state = 0x12345678;

static uint32_t simple_rand(void) {
    // Simple LCG (Linear Congruential Generator)
    rand_state = (1103515245 * rand_state + 12345) & 0x7FFFFFFF;
    return rand_state;
}

//==============================================================================
// Main Entry Point
//==============================================================================

int main(void) {
    uint8_t *block1 = NULL;
    uint8_t *block2 = NULL;
    uint32_t crc1, crc2;
    const uint32_t BLOCK_SIZE = 10 * 1024;  // 10KB (2x10KB = 20KB fits in 24KB heap)

    printf("\r\n");
    printf("========================================\r\n");
    printf("Heap Test - 10KB malloc/memcpy/CRC32\r\n");
    printf("========================================\r\n");
    printf("\r\n");

    // Initialize CRC32 lookup table
    crc32_init();
    printf("Step 1: CRC32 table initialized\r\n");

    // Allocate first 10KB block
    printf("Step 2: Allocating first 10KB block...\r\n");
    block1 = (uint8_t *)malloc(BLOCK_SIZE);
    if (block1 == NULL) {
        printf("ERROR: Failed to allocate block1 (10KB)\r\n");
        printf("Press any key to exit...\r\n");
        while (!uart_getc_available());
        uart_getc();
        return 1;
    }
    printf("  Block1 allocated at: 0x%08lX\r\n", (unsigned long)block1);

    // Fill block1 with random data
    printf("Step 3: Filling block1 with random data...\r\n");
    for (uint32_t i = 0; i < BLOCK_SIZE; i++) {
        block1[i] = (uint8_t)(simple_rand() & 0xFF);
    }
    printf("  Block1 filled with %lu bytes of random data\r\n",
           (unsigned long)BLOCK_SIZE);

    // Calculate CRC32 of block1
    printf("Step 4: Calculating CRC32 of block1...\r\n");
    crc1 = calculate_crc32(block1, BLOCK_SIZE);
    printf("  Block1 CRC32: 0x%08lX\r\n", (unsigned long)crc1);

    // Allocate second 10KB block
    printf("Step 5: Allocating second 10KB block...\r\n");
    block2 = (uint8_t *)malloc(BLOCK_SIZE);
    if (block2 == NULL) {
        printf("ERROR: Failed to allocate block2 (10KB)\r\n");
        printf("Freeing block1...\r\n");
        free(block1);
        printf("Press any key to exit...\r\n");
        while (!uart_getc_available());
        uart_getc();
        return 1;
    }
    printf("  Block2 allocated at: 0x%08lX\r\n", (unsigned long)block2);

    // Copy block1 to block2
    printf("Step 6: Copying block1 to block2 (memcpy)...\r\n");
    memcpy(block2, block1, BLOCK_SIZE);
    printf("  Copied %lu bytes from block1 to block2\r\n",
           (unsigned long)BLOCK_SIZE);

    // Calculate CRC32 of block2
    printf("Step 7: Calculating CRC32 of block2...\r\n");
    crc2 = calculate_crc32(block2, BLOCK_SIZE);
    printf("  Block2 CRC32: 0x%08lX\r\n", (unsigned long)crc2);

    // Compare CRCs
    printf("Step 8: Comparing CRC32 values...\r\n");
    if (crc1 == crc2) {
        printf("  ✓ SUCCESS: CRC32 values match!\r\n");
    } else {
        printf("  ✗ FAILURE: CRC32 values DO NOT match!\r\n");
        printf("    Block1 CRC: 0x%08lX\r\n", (unsigned long)crc1);
        printf("    Block2 CRC: 0x%08lX\r\n", (unsigned long)crc2);
    }

    // Free both blocks
    printf("Step 9: Freeing both blocks...\r\n");
    free(block1);
    printf("  Block1 freed\r\n");
    free(block2);
    printf("  Block2 freed\r\n");

    // Print summary
    printf("\r\n");
    printf("========================================\r\n");
    printf("Test Summary:\r\n");
    printf("========================================\r\n");
    printf("Block size:     %lu KB (%lu bytes)\r\n",
           (unsigned long)(BLOCK_SIZE / 1024), (unsigned long)BLOCK_SIZE);
    printf("Block1 address: 0x%08lX\r\n", (unsigned long)block1);
    printf("Block2 address: 0x%08lX\r\n", (unsigned long)block2);
    printf("Block1 CRC32:   0x%08lX\r\n", (unsigned long)crc1);
    printf("Block2 CRC32:   0x%08lX\r\n", (unsigned long)crc2);
    printf("Result:         %s\r\n", (crc1 == crc2) ? "PASS" : "FAIL");
    printf("========================================\r\n");
    printf("\r\n");

    printf("Press any key to exit...\r\n");

    // Wait for keypress
    while (1) {
        if (uart_getc_available()) {
            uart_getc();
            break;
        }
    }

    return 0;
}

//==============================================================================
// Notes
//==============================================================================

/*
 * Heap Test Overlay:
 *
 * This overlay tests dynamic memory allocation (malloc/free) and data
 * integrity using CRC32 checksums.
 *
 * Test Flow:
 * 1. Initialize CRC32 lookup table
 * 2. Allocate 32KB block1 on heap
 * 3. Fill block1 with pseudo-random data
 * 4. Calculate CRC32 of block1
 * 5. Allocate 32KB block2 on heap
 * 6. Copy block1 to block2 using memcpy
 * 7. Calculate CRC32 of block2
 * 8. Compare CRC32 values (should match)
 * 9. Free both blocks
 * 10. Display summary and wait for keypress
 *
 * Memory Usage:
 * - Overlay heap: 24KB total (0x7A000-0x7FFFF)
 * - Test requires: 20KB (2 x 10KB blocks) + overhead
 * - Should succeed with 24KB heap
 * - Tests malloc/free and data integrity with CRC32
 */
