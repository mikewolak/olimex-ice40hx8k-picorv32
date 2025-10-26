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

// Heap layout (from linker.ld):
// - Code/Data/BSS: 0x00000000 - 0x0003FFFF (256KB APPSRAM)
// - Heap: starts after BSS, ends at 0x00042000
// - Stack: 0x00042000 - 0x00080000 (grows down from 0x00080000)

// Upload buffer: Use heap start + 64KB offset for safety
// This gives room for malloc/heap usage before upload buffer
extern uint32_t __heap_start;
#define UPLOAD_BUFFER_OFFSET  (64 * 1024)  // 64KB offset from heap start
#define UPLOAD_BUFFER_BASE    ((uint32_t)&__heap_start + UPLOAD_BUFFER_OFFSET)

// Maximum upload size: 128KB (matches OVERLAY_EXEC_SIZE in overlay_loader.h)
#define MAX_OVERLAY_SIZE      (128 * 1024)

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

#endif // OVERLAY_UPLOAD_H
