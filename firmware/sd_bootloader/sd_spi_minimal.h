//==============================================================================
// Minimal SD Card SPI Driver for Bootloader - Header
//
// Copyright (c) October 2025 Michael Wolak
// Email: mikewolak@gmail.com, mike@epromfoundry.com
//==============================================================================

#ifndef SD_SPI_MINIMAL_H
#define SD_SPI_MINIMAL_H

#include <stdint.h>

//==============================================================================
// API Functions
//==============================================================================

// Initialize SD card
// Returns: 0 on success, negative error code on failure
int sd_init(void);

// Read multiple sectors from SD card
// Parameters:
//   buffer - Buffer to read data into (must be at least count * 512 bytes)
//   sector - Starting sector number
//   count  - Number of sectors to read
// Returns: 0 on success, negative error code on failure
int sd_read_sectors(uint8_t *buffer, uint32_t sector, uint32_t count);

#endif // SD_SPI_MINIMAL_H
