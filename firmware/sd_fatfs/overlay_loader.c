//==============================================================================
// Overlay Loader Module - Load and Execute Position-Independent Code
//
// Implements loading and execution of overlay binaries from SD card.
//
// Copyright (c) October 2025 Michael Wolak
// Email: mikewolak@gmail.com, mike@epromfoundry.com
//==============================================================================

#include "overlay_loader.h"
#include "hardware.h"
#include <stdio.h>
#include <string.h>

//==============================================================================
// CRC32 Calculation (matches bootloader_fast.c and overlay_upload.c)
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

// Calculate CRC32 of a memory block
uint32_t overlay_calculate_crc32(uint32_t start_addr, uint32_t end_addr) {
    crc32_init();

    uint32_t crc = 0xFFFFFFFF;
    for (uint32_t addr = start_addr; addr <= end_addr; addr++) {
        uint8_t byte = *((uint8_t *)addr);
        crc = (crc >> 8) ^ crc32_table[(crc ^ byte) & 0xFF];
    }
    return ~crc;
}

//==============================================================================
// Browse Overlays on SD Card
//==============================================================================

FRESULT overlay_browse(overlay_list_t *list) {
    DIR dir;
    FILINFO fno;
    FRESULT fr;

    // Initialize list
    list->count = 0;

    // Open /OVERLAYS directory
    fr = f_opendir(&dir, OVERLAY_DIR);
    if (fr != FR_OK) {
        return fr;
    }

    // Scan directory for .BIN files
    while (1) {
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK || fno.fname[0] == 0) {
            break;  // Error or end of directory
        }

        // Skip directories and hidden files
        if (fno.fattrib & AM_DIR) continue;
        if (fno.fname[0] == '.') continue;

        // Check for .BIN extension
        size_t len = strlen(fno.fname);
        if (len < 4) continue;

        if (strcmp(&fno.fname[len - 4], ".BIN") == 0 ||
            strcmp(&fno.fname[len - 4], ".bin") == 0) {

            // Add to list (if space available)
            if (list->count < 16) {
                overlay_info_t *info = &list->overlays[list->count];

                // Copy filename
                strncpy(info->filename, fno.fname, MAX_OVERLAY_NAME - 1);
                info->filename[MAX_OVERLAY_NAME - 1] = '\0';

                // Store file size
                info->size = fno.fsize;

                // Set default load address and entry point
                info->load_addr = OVERLAY_EXEC_BASE;
                info->entry_point = OVERLAY_EXEC_BASE;

                // CRC will be calculated when loaded
                info->crc32 = 0;

                list->count++;
            }
        }
    }

    f_closedir(&dir);
    return FR_OK;
}

//==============================================================================
// Load Overlay from SD Card to RAM
//==============================================================================

FRESULT overlay_load(const char *filename, uint32_t load_addr, overlay_info_t *info) {
    FIL file;
    FRESULT fr;
    UINT bytes_read;
    char path[64];

    // Build full path
    snprintf(path, sizeof(path), "%s/%s", OVERLAY_DIR, filename);

    // Open file
    fr = f_open(&file, path, FA_READ);
    if (fr != FR_OK) {
        printf("Error: Cannot open %s (error %d)\n", path, fr);
        return fr;
    }

    // Get file size
    uint32_t file_size = f_size(&file);

    // Validate size
    if (file_size == 0 || file_size > OVERLAY_EXEC_SIZE) {
        printf("Error: Invalid overlay size %lu bytes (max %lu)\n",
               (unsigned long)file_size, (unsigned long)OVERLAY_EXEC_SIZE);
        f_close(&file);
        return FR_INVALID_PARAMETER;
    }

    printf("Loading overlay: %s\n", filename);
    printf("Size: %lu bytes (%lu KB)\n",
           (unsigned long)file_size,
           (unsigned long)(file_size / 1024));
    printf("Load address: 0x%08lX\n", (unsigned long)load_addr);

    // Read entire file to RAM
    uint8_t *load_ptr = (uint8_t *)load_addr;
    fr = f_read(&file, load_ptr, file_size, &bytes_read);

    if (fr != FR_OK || bytes_read != file_size) {
        printf("Error: Read failed (error %d, read %u/%lu bytes)\n",
               fr, bytes_read, (unsigned long)file_size);
        f_close(&file);
        return fr;
    }

    f_close(&file);

    // Calculate CRC32 of loaded overlay
    uint32_t crc = overlay_calculate_crc32(load_addr, load_addr + file_size - 1);

    printf("CRC32: 0x%08lX\n", (unsigned long)crc);

    // Populate overlay info
    if (info) {
        strncpy(info->filename, filename, MAX_OVERLAY_NAME - 1);
        info->filename[MAX_OVERLAY_NAME - 1] = '\0';
        info->size = file_size;
        info->crc32 = crc;
        info->load_addr = load_addr;
        info->entry_point = load_addr;  // Entry point is at start of overlay
    }

    printf("✓ Overlay loaded successfully\n");
    return FR_OK;
}

//==============================================================================
// Verify Overlay CRC32
//==============================================================================

uint8_t overlay_verify_crc(uint32_t addr, uint32_t size, uint32_t expected_crc) {
    uint32_t calculated_crc = overlay_calculate_crc32(addr, addr + size - 1);

    printf("Verifying CRC32...\n");
    printf("  Expected:   0x%08lX\n", (unsigned long)expected_crc);
    printf("  Calculated: 0x%08lX\n", (unsigned long)calculated_crc);

    if (calculated_crc == expected_crc) {
        printf("✓ CRC32 verified OK\n");
        return 1;
    } else {
        printf("✗ CRC32 MISMATCH!\n");
        return 0;
    }
}

//==============================================================================
// Execute Overlay
//==============================================================================

void overlay_execute(uint32_t entry_point) {
    // Function pointer to overlay entry point
    typedef void (*overlay_func_t)(void);
    overlay_func_t overlay_entry = (overlay_func_t)entry_point;

    printf("\n");
    printf("========================================\n");
    printf("Jumping to overlay at 0x%08lX...\n", (unsigned long)entry_point);
    printf("========================================\n");
    printf("\n");

    // Small delay for printf to flush
    for (volatile int i = 0; i < 100000; i++);

    // Jump to overlay!
    // The overlay is expected to:
    // 1. Run its code
    // 2. Return to this point when done
    // 3. NOT corrupt the stack or SD manager memory
    overlay_entry();

    // If we get here, overlay returned successfully
    printf("\n");
    printf("========================================\n");
    printf("Overlay returned successfully\n");
    printf("========================================\n");
    printf("\n");
}
