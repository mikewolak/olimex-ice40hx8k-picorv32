//==============================================================================
// Olimex iCE40HX8K-EVB RISC-V Platform
// File Browser - Header
//
// Beautiful scrollable file browser with full file management
//
// Copyright (c) October 2025 Michael Wolak
// Email: mikewolak@gmail.com, mike@epromfoundry.com
//==============================================================================

#ifndef FILE_BROWSER_H
#define FILE_BROWSER_H

#include <stdint.h>

//==============================================================================
// File Browser Entry Structure
//==============================================================================

typedef struct {
    char name[256];         // Long filename (LFN) + null (matches FF_LFN_BUF + 1)
    uint32_t size;          // File size in bytes
    uint16_t date;          // FAT date
    uint16_t time;          // FAT time
    uint8_t attrib;         // File attributes
    uint8_t is_dir;         // 1 if directory, 0 if file
} FileEntry;

//==============================================================================
// Function Prototypes
//==============================================================================

void show_file_browser(void);

#endif // FILE_BROWSER_H
