//==============================================================================
// Overlay Loader Module - Load and Execute Position-Independent Code
//
// Loads overlay binaries from SD card to RAM and executes them as
// position-independent code. Overlays can be uploaded via UART and stored
// on SD card, then loaded and run on demand.
//
// Copyright (c) October 2025 Michael Wolak
// Email: mikewolak@gmail.com, mike@epromfoundry.com
//==============================================================================

#ifndef OVERLAY_LOADER_H
#define OVERLAY_LOADER_H

#include <stdint.h>
#include "ff.h"

//==============================================================================
// Memory Configuration (from MEMORY_ALLOCATION.md)
//==============================================================================

// Overlay execution slot (where overlays run from)
#define OVERLAY_EXEC_BASE   0x00060000  // 384 KB (after main firmware heap)
#define OVERLAY_EXEC_SIZE   (96 * 1024)  // 96 KB max overlay size

// Upload buffer configuration now in overlay_upload.h
// (Uses heap start + 64KB offset for safe location)

// Overlay directory on SD card
#define OVERLAY_DIR         "/OVERLAYS"

// Maximum filename length (supports long filenames)
#define MAX_OVERLAY_NAME    256

//==============================================================================
// Overlay Information Structure
//==============================================================================

typedef struct {
    char filename[MAX_OVERLAY_NAME];  // e.g., "HEXEDIT.BIN"
    uint32_t size;                     // File size in bytes
    uint32_t crc32;                    // CRC32 checksum
    uint32_t load_addr;                // Load address (usually OVERLAY_EXEC_BASE)
    uint32_t entry_point;              // Entry point address
} overlay_info_t;

//==============================================================================
// Overlay List Structure (for browsing)
//==============================================================================

typedef struct {
    overlay_info_t overlays[16];  // Max 16 overlays
    uint8_t count;                 // Number of overlays found
} overlay_list_t;

//==============================================================================
// API Functions
//==============================================================================

// Browse overlays on SD card
// Scans /OVERLAYS directory and returns list of available overlays
//
// Parameters:
//   list - Pointer to overlay_list_t to populate
//
// Returns:
//   FR_OK on success
//   FatFS error code on failure
//
FRESULT overlay_browse(overlay_list_t *list);

// Load overlay from SD card to RAM
// Reads overlay binary from SD card and copies to execution address
//
// Parameters:
//   filename  - Name of overlay file (e.g., "HEXEDIT.BIN")
//   load_addr - Address to load overlay (usually OVERLAY_EXEC_BASE)
//   info      - Pointer to overlay_info_t to populate with metadata
//
// Returns:
//   FR_OK on success
//   FatFS error code on failure
//
FRESULT overlay_load(const char *filename, uint32_t load_addr, overlay_info_t *info);

// Verify overlay CRC32
// Calculates CRC32 of overlay in RAM and compares with expected value
//
// Parameters:
//   addr         - Start address of overlay in RAM
//   size         - Size of overlay in bytes
//   expected_crc - Expected CRC32 value
//
// Returns:
//   1 if CRC matches
//   0 if CRC mismatch
//
uint8_t overlay_verify_crc(uint32_t addr, uint32_t size, uint32_t expected_crc);

// Execute overlay
// Jumps to overlay entry point and runs overlay code
// Overlay is expected to return when done
//
// Parameters:
//   entry_point - Address of overlay entry function
//
// Returns:
//   (This function may not return if overlay crashes)
//
void overlay_execute(uint32_t entry_point);

// Calculate CRC32 of memory region (for verification)
// Uses same algorithm as bootloader_fast.c and fw_upload_fast
//
// Parameters:
//   start_addr - Start address of region
//   end_addr   - End address of region (inclusive)
//
// Returns:
//   CRC32 checksum
//
uint32_t overlay_calculate_crc32(uint32_t start_addr, uint32_t end_addr);

#endif // OVERLAY_LOADER_H
