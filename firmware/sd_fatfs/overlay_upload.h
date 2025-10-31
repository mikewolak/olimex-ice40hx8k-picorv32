//==============================================================================
// Overlay Upload Module - UART Upload to SD Card
//
// Implements FAST streaming protocol from bootloader_fast.c to receive
// overlay binaries via UART and save them to /OVERLAYS/ on SD card.
//
// Copyright (c) October 2025 Michael Wolak
// Email: mikewolak@gmail.com, mike@epromfoundry.com
//==============================================================================

#ifndef OVERLAY_UPLOAD_H
#define OVERLAY_UPLOAD_H

#include <stdint.h>
#include "ff.h"

//==============================================================================
// Memory Configuration
//==============================================================================

// Memory layout (from linker.ld):
// - Code/Data/BSS: 0x00000000 - 0x0003FFFF (256KB APPSRAM)
// - Heap: starts after BSS (~0x2E300), ends at 0x00074000 (~280KB heap)
// - Stack: 0x00074000 - 0x00080000 (48KB, grows down from 0x80000)

// Bootloader buffer (192KB) is allocated dynamically from heap
// Heap region: 0x42000-0x74000 (~280KB available)
// Overlays execute at 0x60000 (separate from bootloader upload operation)

// Bootloader upload buffer size: 192KB (allocated from heap)
//
// ⚠️ KNOWN LIMITATION - CANNOT UPLOAD BLOCKS LARGER THAN 64KB:
// -------------------------------------------------------------
// While the buffer is allocated as 192KB, testing has demonstrated that
// uploads larger than 64KB fail with CRC mismatches.
//
// OBSERVED BEHAVIOR:
//    - 64KB:    ✓ WORKS (CRC passes, verify passes)
//    - 64KB+1:  ✓ WORKS (tested and confirmed)
//    - 128KB:   ✗ FAILS (CRC mismatch)
//    - 161KB:   ✗ FAILS (CRC mismatch)
//    - Failure threshold: somewhere between 64KB+1 and 128KB
//
// ROOT CAUSE: UNKNOWN
//    The failure mechanism has not been identified. Possible causes include:
//    - Memory access issues when running firmware from SRAM
//    - malloc() heap fragmentation or allocation issues
//    - Buffer addressing problems beyond 64KB boundary
//    - UART receive issues (though bootloader handles 161KB successfully)
//    - SD card sector write issues
//    - Other unknown factors
//
// CURRENT STATUS:
//    This is a known limitation that requires further investigation.
//    Until the root cause is identified and fixed, uploads are limited
//    to 64KB maximum size.
//
#define BOOTLOADER_UPLOAD_BUFFER_SIZE  (192 * 1024)  // Allocated but only 64KB usable

// Overlay upload still uses 0x60000 region
#define UPLOAD_BUFFER_BASE    0x00060000

// Maximum overlay size: 96KB (leaves room for overlay stack at 0x7A000)
// ⚠️ NOTE: Same 64KB limitation applies to overlay uploads
#define MAX_OVERLAY_SIZE      (96 * 1024)  // Defined as 96KB but only 64KB usable

// Overlay directory on SD card
#define OVERLAY_DIR         "/OVERLAYS"

//==============================================================================
// API
//==============================================================================

// Upload overlay from UART and save to SD card
// Protocol: FAST streaming (matches fw_upload_fast tool)
//
// Parameters:
//   filename - Name for overlay file (e.g., "HEXEDIT.BIN")
//
// Returns:
//   FR_OK on success
//   FatFS error code on failure
//
// Protocol Steps:
//   1. Wait for 'R' from host
//   2. Send 'A' (ack)
//   3. Receive 4-byte size
//   4. Send 'B' (ack)
//   5. Stream ALL data to buffer
//   6. Calculate CRC32
//   7. Receive 'C' + expected CRC
//   8. Send 'C' + calculated CRC
//   9. If CRC match: save to SD card
//
FRESULT overlay_upload(const char *filename);

// Ensure /OVERLAYS directory exists, create if needed
FRESULT overlay_ensure_directory(void);

// Upload overlay to RAM and execute directly (no SD card save)
// Protocol: Same as overlay_upload() but doesn't save to SD
//
// Returns:
//   FR_OK on success
//   FatFS error code on failure
//
FRESULT overlay_upload_and_execute(void);

// Upload bootloader via UART and write to raw sectors 1-1024
// Protocol: FAST streaming (same as overlay upload)
//
// Returns:
//   FR_OK on success
//   FatFS error code on failure
//
// This function uploads bootloader code to the same UPLOAD_BUFFER_BASE
// memory location as overlays, but instead of saving to a file, it
// writes the data directly to raw sectors 1-1024 (512KB bootloader partition).
//
FRESULT bootloader_upload_to_partition(void);

#endif // OVERLAY_UPLOAD_H
