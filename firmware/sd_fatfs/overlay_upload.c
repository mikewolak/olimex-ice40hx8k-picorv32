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
#include <stdio.h>
#include <string.h>

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

// Calculate CRC32 of a memory block (post-receive)
static uint32_t calculate_crc32(uint32_t start_addr, uint32_t end_addr) {
    uint32_t crc = 0xFFFFFFFF;
    for (uint32_t addr = start_addr; addr <= end_addr; addr++) {
        uint8_t byte = *((uint8_t *)addr);
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
        printf("Error: Cannot create /OVERLAYS directory (error %d)\n", fr);
        return fr;
    }

    printf("Waiting for upload from fw_upload_fast...\n");
    printf("Protocol: FAST streaming\n");
    printf("Buffer: 0x%08lX (%lu KB)\n",
           (unsigned long)UPLOAD_BUFFER_BASE,
           (unsigned long)(UPLOAD_BUFFER_SIZE / 1024));

    // Turn on LED to indicate waiting for upload
    LED_REG = 0x01;

    // Step 1: Wait for 'R' (Ready) command
    printf("Step 1: Waiting for 'R' command...\n");
    while (1) {
        uint8_t cmd = uart_getc_raw();
        if (cmd == 'R' || cmd == 'r') {
            break;
        }
    }

    // Step 2: Send ACK 'A' for Ready
    uart_putc_raw('A');
    printf("Step 2: Sent 'A' (ready ACK)\n");

    // LED pattern: LED2 on = downloading
    LED_REG = 0x02;

    // Step 3: Receive 4-byte packet size (little-endian)
    printf("Step 3: Receiving size...\n");
    for (int i = 0; i < 4; i++) {
        uint8_t byte = uart_getc_raw();
        packet_size |= ((uint32_t)byte) << (i * 8);
    }

    printf("Size: %lu bytes (%lu KB)\n",
           (unsigned long)packet_size,
           (unsigned long)(packet_size / 1024));

    // Step 4: Send ACK 'B' for size received
    uart_putc_raw('B');

    // Validate size
    if (packet_size == 0 || packet_size > MAX_OVERLAY_SIZE) {
        printf("Error: Invalid size (max %lu KB)\n",
               (unsigned long)(MAX_OVERLAY_SIZE / 1024));
        LED_REG = 0x00;  // Turn off LEDs = error
        return FR_INVALID_PARAMETER;
    }

    // Step 5: STREAM ALL DATA (no chunking, no ACKs!)
    // This is the key difference from chunked protocols
    printf("Step 5: Streaming data...\n");

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

            // Print progress
            printf("\rReceived: %lu / %lu KB",
                   (unsigned long)(bytes_received / 1024),
                   (unsigned long)(packet_size / 1024));
        }
    }
    printf("\n");

    // Step 6: Calculate CRC32 of received data (post-receive)
    printf("Step 6: Calculating CRC32...\n");
    calculated_crc = calculate_crc32(UPLOAD_BUFFER_BASE,
                                     UPLOAD_BUFFER_BASE + packet_size - 1);
    printf("Calculated CRC: 0x%08lX\n", (unsigned long)calculated_crc);

    // Step 7: Wait for 'C' (CRC command)
    uint8_t crc_cmd = uart_getc_raw();
    if (crc_cmd != 'C') {
        printf("Error: Expected 'C', got 0x%02X\n", crc_cmd);
        LED_REG = 0x00;  // Error
        return FR_INT_ERR;
    }

    // Step 8: Receive 4-byte expected CRC (little-endian)
    expected_crc = 0;
    for (int i = 0; i < 4; i++) {
        uint8_t byte = uart_getc_raw();
        expected_crc |= ((uint32_t)byte) << (i * 8);
    }
    printf("Expected CRC:   0x%08lX\n", (unsigned long)expected_crc);

    // Step 9: Send 'C' + calculated CRC back to host
    uart_putc_raw('C');

    // Send calculated CRC (little-endian)
    uart_putc_raw((calculated_crc >> 0) & 0xFF);
    uart_putc_raw((calculated_crc >> 8) & 0xFF);
    uart_putc_raw((calculated_crc >> 16) & 0xFF);
    uart_putc_raw((calculated_crc >> 24) & 0xFF);

    // Step 10: Verify CRC match
    if (calculated_crc != expected_crc) {
        printf("Error: CRC MISMATCH!\n");
        LED_REG = 0x00;  // Error - CRC mismatch
        return FR_INT_ERR;
    }

    printf("CRC verified OK!\n");

    // Step 11: Save to SD card
    printf("Step 11: Saving to SD card...\n");

    // Build full path: /OVERLAYS/filename
    char path[64];
    snprintf(path, sizeof(path), "%s/%s", OVERLAY_DIR, filename);
    printf("Path: %s\n", path);

    // Open file for writing (create or overwrite)
    fr = f_open(&file, path, FA_WRITE | FA_CREATE_ALWAYS);
    if (fr != FR_OK) {
        printf("Error: Cannot open file (error %d)\n", fr);
        LED_REG = 0x00;
        return fr;
    }

    // Write buffer to file
    fr = f_write(&file, buffer, packet_size, &bytes_written);
    if (fr != FR_OK || bytes_written != packet_size) {
        printf("Error: Write failed (error %d, wrote %u/%lu bytes)\n",
               fr, bytes_written, (unsigned long)packet_size);
        f_close(&file);
        LED_REG = 0x00;
        return fr;
    }

    // Close file
    fr = f_close(&file);
    if (fr != FR_OK) {
        printf("Error: Cannot close file (error %d)\n", fr);
        LED_REG = 0x00;
        return fr;
    }

    // Success! Turn off LEDs
    LED_REG = 0x00;

    printf("\n");
    printf("SUCCESS! Overlay saved to %s\n", path);
    printf("Size: %lu bytes\n", (unsigned long)packet_size);
    printf("CRC32: 0x%08lX\n", (unsigned long)calculated_crc);

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

    printf("Upload and Execute Mode - Direct RAM execution\n");
    printf("Protocol: FAST streaming\n");
    printf("Buffer: 0x%08lX (%lu KB)\n",
           (unsigned long)UPLOAD_BUFFER_BASE,
           (unsigned long)(UPLOAD_BUFFER_SIZE / 1024));

    // Turn on LED to indicate waiting for upload
    LED_REG = 0x01;

    // Step 1: Wait for 'R' (Ready) command
    printf("Step 1: Waiting for 'R' command...\n");
    while (1) {
        uint8_t cmd = uart_getc_raw();
        if (cmd == 'R' || cmd == 'r') {
            break;
        }
    }

    // Step 2: Send ACK 'A' for Ready
    uart_putc_raw('A');
    printf("Step 2: Sent 'A' (ready ACK)\n");

    // LED pattern: LED2 on = downloading
    LED_REG = 0x02;

    // Step 3: Receive 4-byte packet size (little-endian)
    printf("Step 3: Receiving size...\n");
    for (int i = 0; i < 4; i++) {
        uint8_t byte = uart_getc_raw();
        packet_size |= ((uint32_t)byte) << (i * 8);
    }

    printf("Size: %lu bytes (%lu KB)\n",
           (unsigned long)packet_size,
           (unsigned long)(packet_size / 1024));

    // Step 4: Send ACK 'B' for size received
    uart_putc_raw('B');

    // Validate size
    if (packet_size == 0 || packet_size > MAX_OVERLAY_SIZE) {
        printf("Error: Invalid size (max %lu KB)\n",
               (unsigned long)(MAX_OVERLAY_SIZE / 1024));
        LED_REG = 0x00;
        return FR_INVALID_PARAMETER;
    }

    // Step 5: STREAM ALL DATA
    printf("Step 5: Streaming data...\n");

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

            printf("\rReceived: %lu / %lu KB",
                   (unsigned long)(bytes_received / 1024),
                   (unsigned long)(packet_size / 1024));
        }
    }
    printf("\n");

    // Step 6: Calculate CRC32
    printf("Step 6: Calculating CRC32...\n");
    calculated_crc = calculate_crc32(UPLOAD_BUFFER_BASE,
                                     UPLOAD_BUFFER_BASE + packet_size - 1);
    printf("Calculated CRC: 0x%08lX\n", (unsigned long)calculated_crc);

    // Step 7: Wait for 'C' (CRC command)
    uint8_t crc_cmd = uart_getc_raw();
    if (crc_cmd != 'C') {
        printf("Error: Expected 'C', got 0x%02X\n", crc_cmd);
        LED_REG = 0x00;
        return FR_INT_ERR;
    }

    // Step 8: Receive expected CRC
    expected_crc = 0;
    for (int i = 0; i < 4; i++) {
        uint8_t byte = uart_getc_raw();
        expected_crc |= ((uint32_t)byte) << (i * 8);
    }
    printf("Expected CRC:   0x%08lX\n", (unsigned long)expected_crc);

    // Step 9: Send calculated CRC back
    uart_putc_raw('C');
    uart_putc_raw((calculated_crc >> 0) & 0xFF);
    uart_putc_raw((calculated_crc >> 8) & 0xFF);
    uart_putc_raw((calculated_crc >> 16) & 0xFF);
    uart_putc_raw((calculated_crc >> 24) & 0xFF);

    // Step 10: Verify CRC
    if (calculated_crc != expected_crc) {
        printf("Error: CRC MISMATCH!\n");
        LED_REG = 0x00;
        return FR_INT_ERR;
    }

    printf("CRC verified OK!\n");

    // Turn off LEDs
    LED_REG = 0x00;

    // Step 11: Copy to execution space
    printf("\n");
    printf("Copying overlay to execution space (0x18000)...\n");

    uint8_t *src = (uint8_t *)UPLOAD_BUFFER_BASE;
    uint8_t *dst = (uint8_t *)0x00018000;  // Overlay execution base

    for (uint32_t i = 0; i < packet_size; i++) {
        dst[i] = src[i];
    }

    printf("Copy complete!\n");

    // Step 12: Execute overlay
    printf("\n");
    printf("========================================\n");
    printf("Executing overlay from RAM...\n");
    printf("========================================\n");
    printf("\n");

    // Small delay for printf to flush
    for (volatile int i = 0; i < 100000; i++);

    // Jump to overlay entry point
    typedef void (*overlay_func_t)(void);
    overlay_func_t overlay_entry = (overlay_func_t)0x00018000;
    overlay_entry();

    // Overlay returned
    printf("\n");
    printf("========================================\n");
    printf("Overlay returned successfully\n");
    printf("========================================\n");
    printf("\n");

    return FR_OK;
}
