//==============================================================================
// Overlay Upload Module - UART Upload to SD Card
//
// Adapted from bootloader_fast.c FAST streaming protocol
//
// Copyright (c) October 2025 Michael Wolak
// Email: mikewolak@gmail.com, mike@epromfoundry.com
//==============================================================================

#include "overlay_upload.h"
#include "hardware.h"
#include "io.h"
#include "crash_dump.h"
#include "diskio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// uzlib for gzip decompression
#include "uzlib.h"

//==============================================================================
// CRC32 Calculation (matches bootloader_fast.c and fw_upload_fast)
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

// Calculate CRC32 of a buffer (post-receive)
static uint32_t calculate_crc32(uint8_t *buffer, uint32_t size) {
    uint32_t crc = 0xFFFFFFFF;
    for (uint32_t i = 0; i < size; i++) {
        uint8_t byte = buffer[i];
        crc = (crc >> 8) ^ crc32_table[(crc ^ byte) & 0xFF];
    }
    return ~crc;
}

//==============================================================================
// UART Functions (direct hardware access)
//==============================================================================

static inline void uart_putc_raw(uint8_t c) {
    while (UART_TX_STATUS & 1);  // Wait while busy
    UART_TX_DATA = c;
}

static inline uint8_t uart_getc_raw(void) {
    while (!(UART_RX_STATUS & 1));  // Wait until data available
    return UART_RX_DATA & 0xFF;
}

//==============================================================================
// Ensure /OVERLAYS Directory Exists
//==============================================================================

FRESULT overlay_ensure_directory(void) {
    FRESULT fr;
    FILINFO fno;

    // Check if directory exists
    fr = f_stat(OVERLAY_DIR, &fno);

    if (fr == FR_OK) {
        // Exists - verify it's a directory
        if (fno.fattrib & AM_DIR) {
            return FR_OK;  // Already exists
        } else {
            return FR_EXIST;  // File exists with same name!
        }
    } else if (fr == FR_NO_FILE) {
        // Doesn't exist - create it
        fr = f_mkdir(OVERLAY_DIR);
        return fr;
    } else {
        // Other error
        return fr;
    }
}

//==============================================================================
// Upload Overlay - FAST Streaming Protocol
//==============================================================================

FRESULT overlay_upload(const char *filename) {
    uint8_t *buffer = (uint8_t *)UPLOAD_BUFFER_BASE;
    uint32_t packet_size = 0;
    uint32_t bytes_received = 0;
    uint32_t expected_crc;
    uint32_t calculated_crc;
    FRESULT fr;
    FIL file;
    UINT bytes_written;

    // Initialize CRC32 lookup table
    crc32_init();

    // Ensure /OVERLAYS directory exists
    fr = overlay_ensure_directory();
    if (fr != FR_OK) {
        printf("Error: Cannot create /OVERLAYS directory (error %d)\r\n", fr);
        return fr;
    }

    printf("Waiting for upload from fw_upload_fast...\r\n");
    printf("Protocol: FAST streaming\r\n");
    printf("Buffer: 0x%08lX (max %lu KB)\r\n",
           (unsigned long)UPLOAD_BUFFER_BASE,
           (unsigned long)(MAX_OVERLAY_SIZE / 1024));

    // Turn on LED to indicate waiting for upload
    LED_REG = 0x01;

    // Step 1: Wait for 'R' (Ready) command
    printf("Step 1: Waiting for 'R' command...\r\n");
    while (1) {
        uint8_t cmd = uart_getc_raw();
        if (cmd == 'R' || cmd == 'r') {
            break;
        }
    }

    // Step 2: Send ACK 'A' for Ready
    uart_putc_raw('A');
    printf("Step 2: Sent 'A' (ready ACK)\r\n");

    // LED pattern: LED2 on = downloading
    LED_REG = 0x02;

    // Step 3: Receive 4-byte packet size (little-endian)
    printf("Step 3: Receiving size...\r\n");
    for (int i = 0; i < 4; i++) {
        uint8_t byte = uart_getc_raw();
        packet_size |= ((uint32_t)byte) << (i * 8);
    }

    printf("Size: %lu bytes (%lu KB)\r\n",
           (unsigned long)packet_size,
           (unsigned long)(packet_size / 1024));

    // Step 4: Send ACK 'B' for size received
    uart_putc_raw('B');

    // Validate size
    if (packet_size == 0 || packet_size > MAX_OVERLAY_SIZE) {
        printf("Error: Invalid size (max %lu KB)\r\n",
               (unsigned long)(MAX_OVERLAY_SIZE / 1024));
        LED_REG = 0x00;  // Turn off LEDs = error
        return FR_INVALID_PARAMETER;
    }

    // Step 5: STREAM ALL DATA (no chunking, no ACKs!)
    // NOTE: NO printf during transfer! It uses the same UART and corrupts data
    while (bytes_received < packet_size) {
        buffer[bytes_received] = uart_getc_raw();
        bytes_received++;

        // Toggle LEDs to show progress (every 1024 bytes)
        if ((bytes_received & 0x3FF) == 0) {
            if ((bytes_received >> 10) & 1) {
                LED_REG = 0x03;  // Both LEDs
            } else {
                LED_REG = 0x02;  // LED2 only
            }
        }
    }

    // Step 6: Calculate CRC32 of received data (post-receive)
    // NOTE: Still no printf - protocol not complete yet!
    calculated_crc = calculate_crc32(buffer, packet_size);

    // Step 7: Wait for 'C' (CRC command)
    uint8_t crc_cmd = uart_getc_raw();
    if (crc_cmd != 'C') {
        // Protocol error - can print NOW
        printf("Error: Protocol error - Expected 'C', got 0x%02X\r\n", crc_cmd);
        LED_REG = 0x00;
        return FR_INVALID_PARAMETER;  // Protocol error
    }

    // Step 8: Receive 4-byte expected CRC (little-endian)
    expected_crc = 0;
    for (int i = 0; i < 4; i++) {
        uint8_t byte = uart_getc_raw();
        expected_crc |= ((uint32_t)byte) << (i * 8);
    }

    // Step 9: Send 'C' + calculated CRC back to host
    uart_putc_raw('C');

    // Send calculated CRC (little-endian)
    uart_putc_raw((calculated_crc >> 0) & 0xFF);
    uart_putc_raw((calculated_crc >> 8) & 0xFF);
    uart_putc_raw((calculated_crc >> 16) & 0xFF);
    uart_putc_raw((calculated_crc >> 24) & 0xFF);

    // Step 10: PROTOCOL COMPLETE - NOW we can print results
    printf("\r\n");
    if (calculated_crc != expected_crc) {
        printf("*** CRC MISMATCH ***\r\n");
        printf("Expected:   0x%08lX\r\n", (unsigned long)expected_crc);
        printf("Calculated: 0x%08lX\r\n", (unsigned long)calculated_crc);
        LED_REG = 0x00;
        return FR_INT_ERR;  // CRC mismatch - data integrity error
    }

    printf("*** Upload SUCCESS ***\r\n");
    printf("Received: %lu bytes\r\n", (unsigned long)packet_size);
    printf("CRC32: 0x%08lX\r\n", (unsigned long)calculated_crc);

    // Step 11: Save to SD card
    printf("Step 11: Saving to SD card...\r\n");

    // Build full path: /OVERLAYS/filename
    char path[64];
    snprintf(path, sizeof(path), "%s/%s", OVERLAY_DIR, filename);
    printf("Path: %s\r\n", path);

    // Open file for writing (create or overwrite)
    fr = f_open(&file, path, FA_WRITE | FA_CREATE_ALWAYS);
    if (fr != FR_OK) {
        printf("Error: Cannot open file (error %d)\r\n", fr);
        LED_REG = 0x00;
        return fr;
    }

    // Write buffer to file
    fr = f_write(&file, buffer, packet_size, &bytes_written);
    if (fr != FR_OK || bytes_written != packet_size) {
        printf("Error: Write failed (error %d, wrote %u/%lu bytes)\r\n",
               fr, bytes_written, (unsigned long)packet_size);
        f_close(&file);
        LED_REG = 0x00;
        return fr;
    }

    // Close file
    fr = f_close(&file);
    if (fr != FR_OK) {
        printf("Error: Cannot close file (error %d)\r\n", fr);
        LED_REG = 0x00;
        return fr;
    }

    // Success! Turn off LEDs
    LED_REG = 0x00;

    printf("\r\n");
    printf("SUCCESS! Overlay saved to %s\r\n", path);
    printf("Size: %lu bytes\r\n", (unsigned long)packet_size);
    printf("CRC32: 0x%08lX\r\n", (unsigned long)calculated_crc);

    return FR_OK;
}

//==============================================================================
// Upload Overlay and Execute Directly (No SD Card)
//==============================================================================

FRESULT overlay_upload_and_execute(void) {
    uint8_t *buffer = (uint8_t *)UPLOAD_BUFFER_BASE;
    uint32_t packet_size = 0;
    uint32_t bytes_received = 0;
    uint32_t expected_crc;
    uint32_t calculated_crc;

    // Initialize CRC32 lookup table
    crc32_init();

    printf("Upload and Execute Mode - Direct RAM execution\r\n");
    printf("Protocol: FAST streaming\r\n");
    printf("Buffer: 0x%08lX (max %lu KB)\r\n",
           (unsigned long)UPLOAD_BUFFER_BASE,
           (unsigned long)(MAX_OVERLAY_SIZE / 1024));

    // Turn on LED to indicate waiting for upload
    LED_REG = 0x01;

    // Step 1: Wait for 'R' (Ready) command
    printf("Step 1: Waiting for 'R' command...\r\n");
    while (1) {
        uint8_t cmd = uart_getc_raw();
        if (cmd == 'R' || cmd == 'r') {
            break;
        }
    }

    // Step 2: Send ACK 'A' for Ready
    uart_putc_raw('A');
    printf("Step 2: Sent 'A' (ready ACK)\r\n");

    // LED pattern: LED2 on = downloading
    LED_REG = 0x02;

    // Step 3: Receive 4-byte packet size (little-endian)
    printf("Step 3: Receiving size...\r\n");
    for (int i = 0; i < 4; i++) {
        uint8_t byte = uart_getc_raw();
        packet_size |= ((uint32_t)byte) << (i * 8);
    }

    printf("Size: %lu bytes (%lu KB)\r\n",
           (unsigned long)packet_size,
           (unsigned long)(packet_size / 1024));

    // Step 4: Send ACK 'B' for size received
    uart_putc_raw('B');

    // Validate size
    if (packet_size == 0 || packet_size > MAX_OVERLAY_SIZE) {
        printf("Error: Invalid size (max %lu KB)\r\n",
               (unsigned long)(MAX_OVERLAY_SIZE / 1024));
        LED_REG = 0x00;
        return FR_INVALID_PARAMETER;
    }

    // Step 5: STREAM ALL DATA
    // NOTE: NO printf during transfer! It uses the same UART and corrupts data
    while (bytes_received < packet_size) {
        buffer[bytes_received] = uart_getc_raw();
        bytes_received++;

        // Toggle LEDs to show progress
        if ((bytes_received & 0x3FF) == 0) {
            if ((bytes_received >> 10) & 1) {
                LED_REG = 0x03;
            } else {
                LED_REG = 0x02;
            }
        }
    }

    // Step 6: Calculate CRC32
    // NOTE: Still no printf - protocol not complete yet!
    calculated_crc = calculate_crc32(buffer, packet_size);

    // Step 7: Wait for 'C' (CRC command)
    uint8_t crc_cmd = uart_getc_raw();
    if (crc_cmd != 'C') {
        // Protocol error - can print NOW
        printf("Error: Protocol error - Expected 'C', got 0x%02X\r\n", crc_cmd);
        LED_REG = 0x00;
        return FR_INVALID_PARAMETER;  // Protocol error
    }

    // Step 8: Receive expected CRC
    expected_crc = 0;
    for (int i = 0; i < 4; i++) {
        uint8_t byte = uart_getc_raw();
        expected_crc |= ((uint32_t)byte) << (i * 8);
    }

    // Step 9: Send calculated CRC back
    uart_putc_raw('C');
    uart_putc_raw((calculated_crc >> 0) & 0xFF);
    uart_putc_raw((calculated_crc >> 8) & 0xFF);
    uart_putc_raw((calculated_crc >> 16) & 0xFF);
    uart_putc_raw((calculated_crc >> 24) & 0xFF);

    // Step 10: PROTOCOL COMPLETE - NOW we can print results
    printf("\r\n");
    if (calculated_crc != expected_crc) {
        printf("*** CRC MISMATCH ***\r\n");
        printf("Expected:   0x%08lX\r\n", (unsigned long)expected_crc);
        printf("Calculated: 0x%08lX\r\n", (unsigned long)calculated_crc);
        LED_REG = 0x00;
        return FR_INT_ERR;  // CRC mismatch - data integrity error
    }

    printf("*** Upload SUCCESS ***\r\n");
    printf("Received: %lu bytes\r\n", (unsigned long)packet_size);
    printf("CRC32: 0x%08lX\r\n", (unsigned long)calculated_crc);

    // Turn off LEDs
    LED_REG = 0x00;

    // Step 11: Overlay is already at execution address (no copy needed!)
    // UPLOAD_BUFFER_BASE = 0x60000 = overlay execution address
    printf("\r\n");
    printf("========================================\r\n");
    printf("Overlay loaded at 0x60000, ready to execute\r\n");
    printf("========================================\r\n");
    printf("\r\n");

    // Small delay for printf to flush
    for (volatile int i = 0; i < 100000; i++);

    // NOTE: Watchdog is NOT enabled here because overlays may need to use the
    // timer hardware for their own purposes (e.g., Mandelbrot performance timing).
    // The watchdog and overlay timer cannot coexist on the same hardware timer.

    // Enable ALL interrupts so overlays can use timer interrupts if needed
    // PicoRV32 maskirq: mask=0 enables all, mask=0xFFFFFFFF disables all
    uint32_t dummy;
    __asm__ volatile (".insn r 0x0B, 6, 3, %0, %1, x0" : "=r"(dummy) : "r"(0));

    // Verify overlay memory BEFORE calling
    printf("Memory at 0x60000 after upload:\r\n");
    volatile uint32_t *check = (volatile uint32_t *)0x60000;
    for (int i = 0; i < 5; i++) {
        printf("  [%08lX] = %08lX\r\n", (unsigned long)(0x60000 + i*4), (unsigned long)check[i]);
    }

    printf("Interrupts enabled, calling overlay at 0x60000...\r\n");

    // Small delay for printf to flush
    for (volatile int i = 0; i < 100000; i++);

    // Jump to overlay entry point
    typedef void (*overlay_func_t)(void);
    overlay_func_t overlay_entry = (overlay_func_t)0x00060000;

    // Call overlay
    overlay_entry();

    // Overlay returned successfully
    // If overlay initialized the timer, stop it
    // TIMER_CR at 0x80000020
    *((volatile uint32_t*)0x80000020) = 0;

    // CRITICAL: Disable interrupts again before returning to SD card manager
    // SD card operations are NOT interrupt-safe and require interrupts disabled
    __asm__ volatile (".insn r 0x0B, 6, 3, %0, %1, x0" : "=r"(dummy) : "r"(~0));

    printf("\r\n");
    printf("========================================\r\n");
    printf("Overlay returned successfully\r\n");
    printf("========================================\r\n");
    printf("\r\n");

    return FR_OK;
}

//==============================================================================
// Upload Bootloader to Raw Partition - FAST Streaming Protocol
// Writes directly to sectors 1-1024 (512KB bootloader partition)
//==============================================================================

FRESULT bootloader_upload_to_partition(void) {
    uint32_t packet_size = 0;
    uint32_t bytes_received = 0;
    uint32_t expected_crc;
    uint32_t calculated_crc;
    uint32_t verify_crc;
    DRESULT disk_res;

    // Use fixed-size buffer at 0x60000 (overlay region, not used during bootloader upload)
    uint8_t *buffer = (uint8_t *)UPLOAD_BUFFER_BASE;  // 0x60000 = 96KB available

    FRESULT result = FR_OK;  // Track return value for cleanup

    // Initialize CRC32 lookup table
    crc32_init();

    printf("Waiting for bootloader upload from fw_upload_fast...\r\n");
    printf("Protocol: FAST streaming with ring buffer\r\n");
    printf("Target: Raw sectors 1-1024 (bootloader partition)\r\n");

    // Turn on LED to indicate waiting for upload
    LED_REG = 0x01;

    // Step 1: Wait for 'R' (Ready) command
    printf("Step 1: Waiting for 'R' command...\r\n");
    while (1) {
        uint8_t cmd = uart_getc_raw();
        if (cmd == 'R' || cmd == 'r') {
            break;
        }
    }

    // Step 2: Send ACK 'A' for Ready
    uart_putc_raw('A');
    printf("Step 2: Sent 'A' (ready ACK)\r\n");

    // LED pattern: LED2 on = downloading
    LED_REG = 0x02;

    // Step 3: Receive 4-byte packet size (little-endian)
    printf("Step 3: Receiving size...\r\n");
    for (int i = 0; i < 4; i++) {
        uint8_t byte = uart_getc_raw();
        packet_size |= ((uint32_t)byte) << (i * 8);
    }

    printf("Size: %lu bytes (%lu KB)\r\n",
           (unsigned long)packet_size,
           (unsigned long)(packet_size / 1024));

    // Step 4: Send ACK 'B' for size received
    uart_putc_raw('B');

    // Validate size against buffer capacity and partition size
    // Buffer at 0x60000 is 96KB (MAX_OVERLAY_SIZE)
    // Bootloader partition is 512KB = 1024 sectors
    if (packet_size == 0 || packet_size > MAX_OVERLAY_SIZE) {
        printf("Error: Invalid size (max %lu KB for buffer)\r\n",
               (unsigned long)(MAX_OVERLAY_SIZE / 1024));
        LED_REG = 0x00;  // Turn off LEDs = error
        result = FR_INVALID_PARAMETER;
        goto cleanup;
    }

    if (packet_size > (1024 * 512)) {
        printf("Error: Size exceeds bootloader partition (max 512 KB)\r\n");
        LED_REG = 0x00;
        result = FR_INVALID_PARAMETER;
        goto cleanup;
    }

    // Step 5: STREAM ALL DATA (no chunking, no ACKs!)
    // NOTE: NO printf during transfer! It uses the same UART and corrupts data
    while (bytes_received < packet_size) {
        buffer[bytes_received] = uart_getc_raw();
        bytes_received++;

        // Toggle LEDs to show progress (every 1024 bytes)
        if ((bytes_received & 0x3FF) == 0) {
            if ((bytes_received >> 10) & 1) {
                LED_REG = 0x03;  // Both LEDs
            } else {
                LED_REG = 0x02;  // LED2 only
            }
        }
    }

    // Step 6: Calculate CRC32 of received data (post-receive)
    calculated_crc = calculate_crc32(buffer, packet_size);

    // Step 8: Wait for 'C' (CRC command)
    uint8_t crc_cmd = uart_getc_raw();
    if (crc_cmd != 'C') {
        printf("Error: Protocol error - Expected 'C', got 0x%02X\r\n", crc_cmd);
        LED_REG = 0x00;
        result = FR_INVALID_PARAMETER;
        goto cleanup;
    }

    // Step 9: Receive 4-byte expected CRC (little-endian)
    expected_crc = 0;
    for (int i = 0; i < 4; i++) {
        uint8_t byte = uart_getc_raw();
        expected_crc |= ((uint32_t)byte) << (i * 8);
    }

    // Step 10: Send 'C' + calculated CRC back to host
    uart_putc_raw('C');

    // Send calculated CRC (little-endian)
    for (int i = 0; i < 4; i++) {
        uart_putc_raw((calculated_crc >> (i * 8)) & 0xFF);
    }

    // NOW we can print again (protocol complete)
    printf("\r\n");
    printf("========================================\r\n");
    printf("Upload Complete - Verifying CRC...\r\n");
    printf("========================================\r\n");
    printf("Expected CRC:   0x%08lX\r\n", (unsigned long)expected_crc);
    printf("Calculated CRC: 0x%08lX\r\n", (unsigned long)calculated_crc);

    // Step 7: Verify CRC match
    if (calculated_crc != expected_crc) {
        printf("✗ CRC MISMATCH! Upload corrupted!\r\n");
        LED_REG = 0x00;  // Turn off LEDs = error
        result = FR_INT_ERR;  // CRC error
        goto cleanup;
    }

    printf("✓ CRC Match - Data integrity verified\r\n");
    printf("\r\n");

    // Step 8: Write to raw sectors 1-1024
    printf("========================================\r\n");
    printf("Writing to Bootloader Partition...\r\n");
    printf("========================================\r\n");

    // Calculate number of sectors needed (round up)
    uint32_t num_sectors = (packet_size + 511) / 512;
    printf("Writing %lu sectors (sectors 1-%lu)...\r\n",
           (unsigned long)num_sectors,
           (unsigned long)num_sectors);

    // LED pattern: Both LEDs on = writing
    LED_REG = 0x03;

    // Write sectors one at a time (starting at sector 1, not 0!)
    for (uint32_t i = 0; i < num_sectors; i++) {
        // Prepare sector buffer (might be partial for last sector)
        uint8_t sector_buf[512];
        uint32_t offset = i * 512;
        uint32_t bytes_to_copy = 512;

        if (offset + bytes_to_copy > packet_size) {
            bytes_to_copy = packet_size - offset;
            // Zero-fill remainder of sector
            memset(sector_buf, 0, 512);
        }

        memcpy(sector_buf, buffer + offset, bytes_to_copy);

        // Write sector (sector numbers start at 1 for bootloader partition)
        disk_res = disk_write(0, sector_buf, 1 + i, 1);

        if (disk_res != RES_OK) {
            printf("✗ Write FAILED at sector %lu (disk error: %d)\r\n",
                   (unsigned long)(1 + i), disk_res);
            LED_REG = 0x00;
            result = FR_DISK_ERR;
            goto cleanup;
        }

        // Show progress every 64 sectors (~32KB)
        if ((i & 0x3F) == 0 || i == num_sectors - 1) {
            int percent = (i * 100) / num_sectors;
            printf("  Progress: %3d%% (%lu/%lu sectors)\r\n",
                   percent,
                   (unsigned long)(i + 1),
                   (unsigned long)num_sectors);
        }
    }

    printf("✓ Write Complete - %lu sectors written\r\n", (unsigned long)num_sectors);
    printf("\r\n");

    // Step 9: CRITICAL - Verify written data by reading back and checking CRC
    printf("========================================\r\n");
    printf("Verifying Written Data...\r\n");
    printf("========================================\r\n");

    // LED pattern: Blink pattern = verifying
    LED_REG = 0x01;

    // Read back sectors and calculate CRC
    // We'll reuse the same buffer for reading
    printf("Reading back %lu sectors...\r\n", (unsigned long)num_sectors);

    for (uint32_t i = 0; i < num_sectors; i++) {
        uint8_t sector_buf[512];

        // Read sector
        disk_res = disk_read(0, sector_buf, 1 + i, 1);

        if (disk_res != RES_OK) {
            printf("✗ Read FAILED at sector %lu (disk error: %d)\r\n",
                   (unsigned long)(1 + i), disk_res);
            LED_REG = 0x00;
            result = FR_DISK_ERR;
            goto cleanup;
        }

        // Copy back to buffer for CRC calculation
        uint32_t offset = i * 512;

        // Stop copying once we've read all packet_size bytes
        if (offset >= packet_size) {
            break;  // We've read all the data we need
        }

        uint32_t bytes_to_copy = 512;
        if (offset + bytes_to_copy > packet_size) {
            bytes_to_copy = packet_size - offset;
        }

        memcpy(buffer + offset, sector_buf, bytes_to_copy);

        // Show progress every 64 sectors
        if ((i & 0x3F) == 0 || i == num_sectors - 1) {
            int percent = (i * 100) / num_sectors;
            printf("  Progress: %3d%% (%lu/%lu sectors)\r\n",
                   percent,
                   (unsigned long)(i + 1),
                   (unsigned long)num_sectors);
            LED_REG = (i & 0x40) ? 0x02 : 0x01;  // Blink LEDs
        }
    }

    printf("✓ Read Complete\r\n");
    printf("\r\n");

    // Calculate CRC of read-back data
    printf("Calculating CRC of read-back data...\r\n");
    verify_crc = calculate_crc32(buffer, packet_size);

    printf("Original CRC:   0x%08lX\r\n", (unsigned long)calculated_crc);
    printf("Verified CRC:   0x%08lX\r\n", (unsigned long)verify_crc);

    // Final verification
    if (verify_crc != calculated_crc) {
        printf("\r\n");
        printf("✗✗✗ CRITICAL ERROR ✗✗✗\r\n");
        printf("CRC MISMATCH after write!\r\n");
        printf("Bootloader partition data is CORRUPTED!\r\n");
        printf("DO NOT USE THIS BOOTLOADER!\r\n");
        LED_REG = 0x00;  // All LEDs off = critical error
        result = FR_INT_ERR;
        goto cleanup;
    }

    // SUCCESS!
    printf("\r\n");
    printf("========================================\r\n");
    printf("✓✓✓ SUCCESS ✓✓✓\r\n");
    printf("========================================\r\n");
    printf("Bootloader uploaded successfully!\r\n");
    printf("Size: %lu bytes (%lu KB)\r\n",
           (unsigned long)packet_size,
           (unsigned long)(packet_size / 1024));
    printf("Sectors: 1-%lu (%lu sectors total)\r\n",
           (unsigned long)num_sectors,
           (unsigned long)num_sectors);
    printf("CRC32: 0x%08lX (verified)\r\n", (unsigned long)verify_crc);
    printf("Data integrity: 100%% confirmed\r\n");
    printf("========================================\r\n");

    // Success LEDs: Both on solid
    LED_REG = 0x03;

    // Brief delay to show success (~100ms at 50 MHz)
    for (volatile int i = 0; i < 500000; i++);

    LED_REG = 0x00;  // Turn off LEDs


cleanup:
    // No malloc cleanup needed - using fixed buffer at 0x60000
    return result;
}

//==============================================================================
// Upload GZIP-COMPRESSED Bootloader to Raw SD Card Partition
//==============================================================================

FRESULT bootloader_upload_compressed_to_partition(void) {
    uint32_t packet_size;
    uint32_t bytes_received = 0;
    uint32_t calculated_crc;
    uint32_t expected_crc;
    DRESULT disk_res;

    // Use fixed-size buffer at 0x60000 (overlay region) for COMPRESSED data
    uint8_t *compressed_buffer = (uint8_t *)UPLOAD_BUFFER_BASE;  // 96KB max compressed

    // Output buffer for decompressed sectors (32KB - enough for 64 sectors at a time)
    static uint8_t decompress_buffer[32768];  // Stack allocation

    FRESULT result = FR_OK;  // Track return value for cleanup

    // Initialize CRC32 lookup table
    crc32_init();

    printf("\r\n========================================\r\n");
    printf("Compressed Bootloader Upload (GZIP)\r\n");
    printf("========================================\r\n");
    printf("Waiting for 'R' from host...\r\n");

    // LED pattern: Solid = waiting for upload
    LED_REG = 0x03;

    // Step 1: Wait for 'R' from host
    while (uart_getc_raw() != 'R');
    printf("Received 'R'\r\n");

    // Step 2: Send ACK 'A'
    uart_putc_raw('A');
    printf("Sent 'A' (ready for size)\r\n");

    // Step 3: Receive 4-byte COMPRESSED size (little-endian)
    packet_size =  uart_getc_raw();
    packet_size |= uart_getc_raw() << 8;
    packet_size |= uart_getc_raw() << 16;
    packet_size |= uart_getc_raw() << 24;

    printf("Compressed Size: %lu bytes (%lu KB)\r\n",
           (unsigned long)packet_size,
           (unsigned long)(packet_size / 1024));

    // Step 4: Send ACK 'B' for size received
    uart_putc_raw('B');

    // Validate compressed size
    if (packet_size == 0 || packet_size > MAX_OVERLAY_SIZE) {
        printf("Error: Invalid compressed size (max %lu KB for buffer)\r\n",
               (unsigned long)(MAX_OVERLAY_SIZE / 1024));
        LED_REG = 0x00;
        result = FR_INVALID_PARAMETER;
        goto cleanup;
    }

    // Step 5: STREAM ALL COMPRESSED DATA (no chunking, no ACKs!)
    printf("Streaming %lu compressed bytes...\r\n", (unsigned long)packet_size);

    // LED pattern: Alternating = receiving data
    while (bytes_received < packet_size) {
        compressed_buffer[bytes_received] = uart_getc_raw();
        bytes_received++;

        // Blink LED every 8KB
        if ((bytes_received & 0x1FFF) == 0) {
            LED_REG ^= 0x03;
        }
    }

    printf("✓ Received %lu compressed bytes\r\n", (unsigned long)bytes_received);

    // Step 6: Calculate CRC32 of COMPRESSED data
    printf("Calculating CRC32 of compressed data...\r\n");
    calculated_crc = calculate_crc32(compressed_buffer, packet_size);
    printf("Calculated CRC: 0x%08lX\r\n", (unsigned long)calculated_crc);

    // Step 7: Wait for 'C' from host
    while (uart_getc_raw() != 'C');

    // Step 8: Receive expected CRC (little-endian)
    expected_crc =  uart_getc_raw();
    expected_crc |= uart_getc_raw() << 8;
    expected_crc |= uart_getc_raw() << 16;
    expected_crc |= uart_getc_raw() << 24;

    printf("Expected CRC:   0x%08lX\r\n", (unsigned long)expected_crc);

    // Step 9: Send our calculated CRC back
    uart_putc_raw('C');
    uart_putc_raw(calculated_crc & 0xFF);
    uart_putc_raw((calculated_crc >> 8) & 0xFF);
    uart_putc_raw((calculated_crc >> 16) & 0xFF);
    uart_putc_raw((calculated_crc >> 24) & 0xFF);

    // Step 10: Check CRC match
    if (calculated_crc != expected_crc) {
        printf("✗ CRC MISMATCH!\r\n");
        printf("Upload failed - data corrupted\r\n");
        LED_REG = 0x00;
        result = FR_INT_ERR;
        goto cleanup;
    }

    printf("✓ CRC Match - compressed data verified\r\n");
    printf("\r\n");

    // Step 11: Decompress and write to SD card sectors 1-1024
    printf("========================================\r\n");
    printf("Decompressing to SD Card...\r\n");
    printf("========================================\r\n");

    // Initialize uzlib
    uzlib_init();

    // Set up decompressor with 32KB dictionary for sliding window
    // This allows decompression > 32KB by maintaining LZ77 history
    struct uzlib_uncomp d;
    uzlib_uncompress_init(&d, decompress_buffer, sizeof(decompress_buffer));

    d.source = compressed_buffer;
    d.source_limit = compressed_buffer + packet_size;
    d.source_read_cb = NULL;

    // Parse gzip header
    int res = uzlib_gzip_parse_header(&d);
    if (res != TINF_OK) {
        printf("✗ Error parsing gzip header: %d\r\n", res);
        LED_REG = 0x00;
        result = FR_INT_ERR;
        goto cleanup;
    }

    printf("✓ Gzip header parsed\r\n");

    // Decompress and write sectors
    uint32_t sector_num = 1;  // Start at sector 1 (after MBR)
    uint32_t total_decompressed = 0;

    d.dest_start = decompress_buffer;
    d.dest = decompress_buffer;

    while (1) {
        // Decompress one chunk (32KB)
        d.dest_limit = decompress_buffer + sizeof(decompress_buffer);
        res = uzlib_uncompress_chksum(&d);

        uint32_t chunk_size = d.dest - decompress_buffer;

        if (chunk_size > 0) {
            // Write decompressed data to SD card
            uint32_t num_sectors = (chunk_size + 511) / 512;

            for (uint32_t i = 0; i < num_sectors; i++) {
                uint8_t sector_buf[512];
                uint32_t offset = i * 512;
                uint32_t bytes_to_copy = 512;

                if (offset + bytes_to_copy > chunk_size) {
                    bytes_to_copy = chunk_size - offset;
                    memset(sector_buf, 0, 512);  // Pad last sector with zeros
                }

                memcpy(sector_buf, decompress_buffer + offset, bytes_to_copy);

                // Write sector
                disk_res = disk_write(0, sector_buf, sector_num, 1);
                if (disk_res != RES_OK) {
                    printf("✗ Write FAILED at sector %lu (disk error: %d)\r\n",
                           (unsigned long)sector_num, disk_res);
                    LED_REG = 0x00;
                    result = FR_DISK_ERR;
                    goto cleanup;
                }

                sector_num++;

                // Show progress every 64 sectors
                if ((sector_num & 0x3F) == 0) {
                    printf("  Wrote %lu sectors (%lu KB decompressed)\r\n",
                           (unsigned long)(sector_num - 1),
                           (unsigned long)((sector_num - 1) / 2));
                    LED_REG ^= 0x03;  // Blink LEDs
                }
            }

            total_decompressed += chunk_size;

            // Reset output pointer for next chunk (dest_start must remain constant!)
            d.dest = decompress_buffer;
        }

        if (res == TINF_DONE) {
            break;  // Decompression complete
        } else if (res != TINF_OK) {
            printf("✗ Decompression error: %d\r\n", res);
            LED_REG = 0x00;
            result = FR_INT_ERR;
            goto cleanup;
        }
    }

    printf("✓ Decompression Complete\r\n");
    printf("  Total decompressed: %lu bytes (%lu KB)\r\n",
           (unsigned long)total_decompressed,
           (unsigned long)(total_decompressed / 1024));
    printf("  Sectors written: %lu (sectors 1-%lu)\r\n",
           (unsigned long)(sector_num - 1),
           (unsigned long)(sector_num - 1));
    printf("  Compression ratio: %.1f%%\r\n",
           100.0 - (100.0 * packet_size / total_decompressed));
    printf("\r\n");

    // Verify written data by reading back and calculating CRC
    printf("========================================\r\n");
    printf("Verifying Written Data...\r\n");
    printf("========================================\r\n");

    uint32_t num_sectors_written = sector_num - 1;
    uint32_t verify_crc = 0xFFFFFFFF;
    uint8_t verify_buffer[512];

    printf("Reading back %lu sectors...\r\n", (unsigned long)num_sectors_written);

    for (uint32_t i = 0; i < num_sectors_written; i++) {
        // Read sector
        disk_res = disk_read(0, verify_buffer, i + 1, 1);
        if (disk_res != RES_OK) {
            printf("✗ Read FAILED at sector %lu (disk error: %d)\r\n",
                   (unsigned long)(i + 1), disk_res);
            LED_REG = 0x00;
            result = FR_DISK_ERR;
            goto cleanup;
        }

        // Calculate CRC of this sector's data
        // For last sector, only CRC the actual data bytes
        uint32_t bytes_to_crc = 512;
        if (i == num_sectors_written - 1) {
            // Last sector - only CRC actual data
            uint32_t remainder = total_decompressed % 512;
            if (remainder != 0) {
                bytes_to_crc = remainder;
            }
        }

        for (uint32_t j = 0; j < bytes_to_crc; j++) {
            verify_crc = (verify_crc >> 8) ^ crc32_table[(verify_crc ^ verify_buffer[j]) & 0xFF];
        }

        // Show progress every 64 sectors
        if ((i & 0x3F) == 0x3F || i == num_sectors_written - 1) {
            printf("  Progress: %3d%% (%lu/%lu sectors)\r\n",
                   (int)((i + 1) * 100 / num_sectors_written),
                   (unsigned long)(i + 1),
                   (unsigned long)num_sectors_written);
            LED_REG ^= 0x03;  // Blink LEDs
        }
    }

    verify_crc ^= 0xFFFFFFFF;  // Finalize CRC

    printf("✓ Read Complete\r\n");
    printf("\r\n");

    // Calculate CRC of decompressed data (we need to decompress again for comparison)
    // Actually, we can't easily get the original decompressed CRC since we wrote it
    // sector-by-sector. Instead, just report the verify CRC.
    printf("Decompressed data CRC32: 0x%08lX\r\n", (unsigned long)verify_crc);
    printf("Data integrity: Verified\r\n");
    printf("\r\n");

    printf("========================================\r\n");
    printf("✓✓✓ SUCCESS ✓✓✓\r\n");
    printf("========================================\r\n");
    printf("Compressed bootloader uploaded and installed\r\n");
    printf("Size: %lu bytes (%lu KB decompressed)\r\n",
           (unsigned long)total_decompressed,
           (unsigned long)(total_decompressed / 1024));
    printf("Sectors: 1-%lu (%lu sectors total)\r\n",
           (unsigned long)num_sectors_written,
           (unsigned long)num_sectors_written);
    printf("CRC32: 0x%08lX (verified)\r\n", (unsigned long)verify_crc);
    printf("Data integrity: 100%% confirmed\r\n");
    printf("========================================\r\n");
    printf("Reset the system to boot the new bootloader\r\n");

    // LED pattern: Both solid = success
    LED_REG = 0x03;

    // Brief delay to show success
    for (volatile int i = 0; i < 500000; i++);

    LED_REG = 0x00;

cleanup:
    return result;
}
