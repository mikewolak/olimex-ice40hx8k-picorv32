//==============================================================================
// FatFS Disk I/O Interface for SD Card
//==============================================================================

#include "ff.h"
#include "diskio.h"
#include "sd_spi.h"

//==============================================================================
// Disk Status
//==============================================================================

DSTATUS disk_status(BYTE pdrv) {
    if (pdrv != 0) {
        return STA_NOINIT;
    }

    // Check if card is initialized
    if (sd_get_card_type() == CARD_TYPE_UNKNOWN) {
        return STA_NOINIT;
    }

    return 0;  // OK
}

//==============================================================================
// Initialize Disk
//==============================================================================

DSTATUS disk_initialize(BYTE pdrv) {
    if (pdrv != 0) {
        return STA_NOINIT;
    }

    // Check if card is already initialized
    if (sd_get_card_type() != CARD_TYPE_UNKNOWN) {
        return 0;  // Already initialized, OK
    }

    // Initialize card if not already done
    uint8_t result = sd_init();
    if (result != SD_OK) {
        return STA_NOINIT;
    }

    return 0;  // OK
}

//==============================================================================
// Read Sectors
//==============================================================================

DRESULT disk_read(
    BYTE pdrv,      // Physical drive number
    BYTE *buff,     // Data buffer to store read data
    LBA_t sector,   // Start sector number (LBA)
    UINT count      // Number of sectors to read
) {
    if (pdrv != 0 || count == 0) {
        return RES_PARERR;
    }

    if (sd_get_card_type() == CARD_TYPE_UNKNOWN) {
        return RES_NOTRDY;
    }

    for (UINT i = 0; i < count; i++) {
        uint8_t result = sd_read_block(sector + i, buff + (i * 512));
        if (result != SD_OK) {
            return RES_ERROR;
        }
    }

    return RES_OK;
}

//==============================================================================
// Write Sectors
//==============================================================================

#if FF_FS_READONLY == 0

DRESULT disk_write(
    BYTE pdrv,          // Physical drive number
    const BYTE *buff,   // Data to be written
    LBA_t sector,       // Start sector number (LBA)
    UINT count          // Number of sectors to write
) {
    if (pdrv != 0 || count == 0) {
        return RES_PARERR;
    }

    if (sd_get_card_type() == CARD_TYPE_UNKNOWN) {
        return RES_NOTRDY;
    }

    for (UINT i = 0; i < count; i++) {
        uint8_t result = sd_write_block(sector + i, buff + (i * 512));
        if (result != SD_OK) {
            return RES_ERROR;
        }
    }

    return RES_OK;
}

#endif

//==============================================================================
// I/O Control
//==============================================================================

DRESULT disk_ioctl(
    BYTE pdrv,      // Physical drive number
    BYTE cmd,       // Control code
    void *buff      // Buffer to send/receive data
) {
    if (pdrv != 0) {
        return RES_PARERR;
    }

    if (sd_get_card_type() == CARD_TYPE_UNKNOWN) {
        return RES_NOTRDY;
    }

    switch (cmd) {
        case CTRL_SYNC:
            // No cache, operation completes immediately
            return RES_OK;

        case GET_SECTOR_COUNT:
            *(LBA_t*)buff = sd_get_sector_count();
            return RES_OK;

        case GET_SECTOR_SIZE:
            *(WORD*)buff = 512;
            return RES_OK;

        case GET_BLOCK_SIZE:
            *(DWORD*)buff = 1;  // Erase block size in sectors
            return RES_OK;

        default:
            return RES_PARERR;
    }
}
