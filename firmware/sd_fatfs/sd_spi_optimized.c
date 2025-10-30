//==============================================================================
// SD Card SPI Driver Implementation - OPTIMIZED WITH BURST TRANSFERS
//
// Uses SPI burst mode for 512-byte block transfers
// Expected performance: ~2.8x improvement over baseline
//==============================================================================

#include "sd_spi.h"
#include "io.h"
#include <string.h>

//==============================================================================
// Static Variables
//==============================================================================

static sd_card_type_t s_card_type = CARD_TYPE_UNKNOWN;
static uint32_t s_sector_count = 0;

//==============================================================================
// SD Card Command Functions
//==============================================================================

static uint8_t sd_send_cmd(uint8_t cmd, uint32_t arg) {
    uint8_t r1;
    uint8_t crc = 0xFF;

    // Special CRC for CMD0 and CMD8
    if (cmd == CMD0) crc = 0x95;
    if (cmd == CMD8) crc = 0x87;

    // Send command packet
    spi_transfer(0x40 | cmd);
    spi_transfer((arg >> 24) & 0xFF);
    spi_transfer((arg >> 16) & 0xFF);
    spi_transfer((arg >> 8) & 0xFF);
    spi_transfer(arg & 0xFF);
    spi_transfer(crc);

    // Wait for response (R1), max 10 attempts
    for (int i = 0; i < 10; i++) {
        r1 = spi_transfer(0xFF);
        if (!(r1 & 0x80)) {
            return r1;
        }
    }

    return 0xFF;  // Timeout
}

static uint8_t sd_send_acmd(uint8_t cmd, uint32_t arg) {
    sd_send_cmd(CMD55, 0);
    return sd_send_cmd(cmd, arg);
}

//==============================================================================
// Initialization
//==============================================================================

void sd_spi_init(void) {
    // Initialize SPI peripheral
    spi_set_speed(SPI_CLK_390KHZ);  // Start slow for initialization
    spi_cs_deassert();
}

uint8_t sd_init(void) {
    uint8_t r1;
    int retry;

    // Reset card type
    s_card_type = CARD_TYPE_UNKNOWN;
    s_sector_count = 0;

    // Set slow speed for initialization
    spi_set_speed(SPI_CLK_390KHZ);

    // Send 80+ dummy clocks with CS high
    spi_cs_deassert();
    for (int i = 0; i < 10; i++) {
        spi_transfer(0xFF);
    }

    // Assert CS
    spi_cs_assert();

    // CMD0: Reset card to idle state
    r1 = sd_send_cmd(CMD0, 0);
    if (r1 != R1_IDLE_STATE) {
        spi_cs_deassert();
        return SD_ERROR_INIT;
    }

    // CMD8: Check voltage range (SDv2 cards)
    r1 = sd_send_cmd(CMD8, 0x1AA);
    if (r1 == R1_IDLE_STATE) {
        // SDv2 card
        uint8_t ocr[4];
        for (int i = 0; i < 4; i++) {
            ocr[i] = spi_transfer(0xFF);
        }

        // Check if card accepted voltage range
        if (ocr[2] == 0x01 && ocr[3] == 0xAA) {
            // Initialize with ACMD41 with HCS bit
            retry = 0;
            do {
                r1 = sd_send_acmd(ACMD41, 0x40000000);
                retry++;
                if (retry > 1000) {
                    spi_cs_deassert();
                    return SD_ERROR_TIMEOUT;
                }
            } while (r1 != 0x00);

            // Read OCR to check CCS bit
            if (sd_send_cmd(CMD58, 0) == 0) {
                for (int i = 0; i < 4; i++) {
                    ocr[i] = spi_transfer(0xFF);
                }
                // Check CCS bit (bit 30 of OCR)
                if (ocr[0] & 0x40) {
                    s_card_type = CARD_TYPE_SDHC;
                } else {
                    s_card_type = CARD_TYPE_SD2;
                }
            }
        }
    } else {
        // SDv1 or MMC card
        retry = 0;
        do {
            r1 = sd_send_acmd(ACMD41, 0);
            retry++;
            if (retry > 1000) {
                spi_cs_deassert();
                return SD_ERROR_CARD_TYPE;
            }
        } while (r1 != 0x00);

        s_card_type = CARD_TYPE_SD1;

        // Set block length to 512 bytes for SDv1
        sd_send_cmd(CMD16, 512);
    }

    // Deassert CS
    spi_cs_deassert();

    // Increase speed to 12.5 MHz for data transfers
    spi_set_speed(SPI_CLK_12MHZ);

    // Read CSD to get card capacity
    sd_csd_t csd;
    if (sd_read_csd(&csd) != SD_OK) {
        return SD_ERROR_READ;
    }

    return SD_OK;
}

//==============================================================================
// Configuration
//==============================================================================

void sd_set_speed(uint32_t speed) {
    spi_set_speed(speed);
}

//==============================================================================
// Card Information
//==============================================================================

sd_card_type_t sd_get_card_type(void) {
    return s_card_type;
}

uint32_t sd_get_sector_count(void) {
    // TODO: Read from CSD register
    return s_sector_count;
}

uint8_t sd_read_cid(sd_cid_t *cid) {
    uint8_t r1;
    uint8_t buffer[16];

    spi_cs_assert();

    // Send CMD10 (SEND_CID)
    r1 = sd_send_cmd(CMD10, 0);
    if (r1 != 0x00) {
        spi_cs_deassert();
        return SD_ERROR_READ;
    }

    // Wait for data token (0xFE)
    uint16_t timeout = 0xFFFF;
    while (spi_transfer(0xFF) != 0xFE) {
        if (--timeout == 0) {
            spi_cs_deassert();
            return SD_ERROR_TIMEOUT;
        }
    }

    // Read 16 bytes of CID data
    for (uint8_t i = 0; i < 16; i++) {
        buffer[i] = spi_transfer(0xFF);
    }

    // Read CRC (2 bytes) - ignored
    spi_transfer(0xFF);
    spi_transfer(0xFF);

    spi_cs_deassert();

    // Parse CID register
    cid->mid = buffer[0];
    cid->oid[0] = buffer[1];
    cid->oid[1] = buffer[2];
    cid->pnm[0] = buffer[3];
    cid->pnm[1] = buffer[4];
    cid->pnm[2] = buffer[5];
    cid->pnm[3] = buffer[6];
    cid->pnm[4] = buffer[7];
    cid->pnm[5] = '\0';
    cid->prv = buffer[8];
    cid->psn = ((uint32_t)buffer[9] << 24) | ((uint32_t)buffer[10] << 16) |
               ((uint32_t)buffer[11] << 8) | buffer[12];
    cid->mdt = ((uint16_t)(buffer[13] & 0x0F) << 8) | buffer[14];

    return SD_OK;
}

uint8_t sd_read_csd(sd_csd_t *csd) {
    uint8_t r1;
    uint8_t buffer[16];

    spi_cs_assert();

    // Send CMD9 (SEND_CSD)
    r1 = sd_send_cmd(CMD9, 0);
    if (r1 != 0x00) {
        spi_cs_deassert();
        return SD_ERROR_READ;
    }

    // Wait for data token (0xFE)
    uint16_t timeout = 0xFFFF;
    while (spi_transfer(0xFF) != 0xFE) {
        if (--timeout == 0) {
            spi_cs_deassert();
            return SD_ERROR_TIMEOUT;
        }
    }

    // Read 16 bytes of CSD data
    for (uint8_t i = 0; i < 16; i++) {
        buffer[i] = spi_transfer(0xFF);
    }

    // Read CRC (2 bytes) - ignored
    spi_transfer(0xFF);
    spi_transfer(0xFF);

    spi_cs_deassert();

    // Parse CSD register - differs between CSD v1.0 and v2.0
    uint8_t csd_version = (buffer[0] >> 6) & 0x03;

    if (csd_version == 0) {
        // CSD v1.0 (SDSC)
        uint16_t c_size = ((uint16_t)(buffer[6] & 0x03) << 10) |
                          ((uint16_t)buffer[7] << 2) |
                          ((uint16_t)(buffer[8] >> 6) & 0x03);
        uint8_t c_size_mult = ((buffer[9] & 0x03) << 1) | ((buffer[10] >> 7) & 0x01);
        uint8_t read_bl_len = buffer[5] & 0x0F;

        // Calculate sector count: (C_SIZE + 1) * 2^(C_SIZE_MULT + 2) * 2^READ_BL_LEN / 512
        s_sector_count = ((uint32_t)(c_size + 1)) << (c_size_mult + read_bl_len - 7);
    } else if (csd_version == 1) {
        // CSD v2.0 (SDHC/SDXC)
        uint32_t c_size = ((uint32_t)(buffer[7] & 0x3F) << 16) |
                          ((uint32_t)buffer[8] << 8) |
                          buffer[9];

        // Calculate sector count: (C_SIZE + 1) * 512K / 512
        s_sector_count = (c_size + 1) * 1024;
    } else {
        return SD_ERROR_CARD_TYPE;
    }

    // Parse other CSD fields
    csd->tran_speed = buffer[3];
    csd->wp = (buffer[14] & 0x30) ? 1 : 0;

    return SD_OK;
}

//==============================================================================
// Data Transfer
//==============================================================================

uint8_t sd_read_block(uint32_t sector, uint8_t *buffer) {
    uint8_t r1;

    // For SDSC cards, sector address is byte address
    if (s_card_type != CARD_TYPE_SDHC) {
        sector <<= 9;  // Convert to byte address
    }

    spi_cs_assert();

    // Send CMD17 (READ_SINGLE_BLOCK)
    r1 = sd_send_cmd(CMD17, sector);
    if (r1 != 0x00) {
        spi_cs_deassert();
        return SD_ERROR_READ;
    }

    // Wait for data token (0xFE)
    uint16_t timeout = 0xFFFF;
    while (spi_transfer(0xFF) != 0xFE) {
        if (--timeout == 0) {
            spi_cs_deassert();
            return SD_ERROR_TIMEOUT;
        }
    }

    // Read 512 bytes using burst mode (2.8x faster than single-byte)
    spi_burst_transfer(NULL, buffer, 512);

    // Read CRC (2 bytes) - ignored for now
    spi_transfer(0xFF);
    spi_transfer(0xFF);

    spi_cs_deassert();

    return SD_OK;
}

uint8_t sd_write_block(uint32_t sector, const uint8_t *buffer) {
    uint8_t r1;

    // For SDSC cards, sector address is byte address
    if (s_card_type != CARD_TYPE_SDHC) {
        sector <<= 9;  // Convert to byte address
    }

    spi_cs_assert();

    // Send CMD24 (WRITE_BLOCK)
    r1 = sd_send_cmd(CMD24, sector);
    if (r1 != 0x00) {
        spi_cs_deassert();
        return SD_ERROR_WRITE;
    }

    // Send data token
    spi_transfer(0xFE);

    // Write 512 bytes using burst mode (2.8x faster than single-byte)
    spi_burst_transfer(buffer, NULL, 512);

    // Send dummy CRC
    spi_transfer(0xFF);
    spi_transfer(0xFF);

    // Read data response
    uint8_t resp = spi_transfer(0xFF);
    if ((resp & 0x1F) != 0x05) {
        spi_cs_deassert();
        return SD_ERROR_WRITE;
    }

    // Wait for card to finish writing (busy)
    uint16_t timeout = 0xFFFF;
    while (spi_transfer(0xFF) == 0x00) {
        if (--timeout == 0) {
            spi_cs_deassert();
            return SD_ERROR_TIMEOUT;
        }
    }

    spi_cs_deassert();

    return SD_OK;
}

//==============================================================================
// Utility
//==============================================================================

const char* sd_get_error_string(uint8_t error) {
    switch (error) {
        case SD_OK:              return "Success";
        case SD_ERROR_INIT:      return "Initialization failed";
        case SD_ERROR_TIMEOUT:   return "Timeout";
        case SD_ERROR_READ:      return "Read error";
        case SD_ERROR_WRITE:     return "Write error";
        case SD_ERROR_CRC:       return "CRC error";
        case SD_ERROR_NOT_READY: return "Card not ready";
        case SD_ERROR_CARD_TYPE: return "Unknown card type";
        default:                 return "Unknown error";
    }
}
