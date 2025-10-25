//==============================================================================
// SD Card SPI Driver Implementation
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
    // TODO: Implement CSD reading

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
    // TODO: Implement CID reading
    memset(cid, 0, sizeof(sd_cid_t));
    return SD_OK;
}

uint8_t sd_read_csd(sd_csd_t *csd) {
    // TODO: Implement CSD reading
    memset(csd, 0, sizeof(sd_csd_t));
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

    // Read 512 bytes
    for (uint16_t i = 0; i < 512; i++) {
        buffer[i] = spi_transfer(0xFF);
    }

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

    // Write 512 bytes
    for (uint16_t i = 0; i < 512; i++) {
        spi_transfer(buffer[i]);
    }

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
