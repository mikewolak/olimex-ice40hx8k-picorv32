//==============================================================================
// Minimal SD Card SPI Driver for Bootloader
//
// Stripped-down version with only what's needed to initialize SD card
// and read sectors. No write support, no FatFS, minimal error handling.
//
// Copyright (c) October 2025 Michael Wolak
// Email: mikewolak@gmail.com, mike@epromfoundry.com
//==============================================================================

#include "sd_spi_minimal.h"
#include <stdint.h>

//==============================================================================
// Hardware Registers
//==============================================================================

#define SPI_BASE        0x80000050
#define SPI_CTRL        (*(volatile uint32_t*)(SPI_BASE + 0x00))
#define SPI_DATA        (*(volatile uint32_t*)(SPI_BASE + 0x04))
#define SPI_STATUS      (*(volatile uint32_t*)(SPI_BASE + 0x08))
#define SPI_CS_REG      (*(volatile uint32_t*)(SPI_BASE + 0x0C))

// SPI Status bits
#define SPI_STATUS_BUSY (1 << 0)  // Transfer in progress

// SPI Clock speeds (divider values - bits [4:2] of CTRL register)
#define SPI_CLK_390KHZ  (7 << 2)  // /128 = 390 KHz (init speed)
#define SPI_CLK_12MHZ   (2 << 2)  // /4  = 12.5 MHz (fast speed)

//==============================================================================
// SD Card Commands
//==============================================================================

#define CMD0    0   // GO_IDLE_STATE
#define CMD8    8   // SEND_IF_COND
#define CMD17   17  // READ_SINGLE_BLOCK
#define CMD55   55  // APP_CMD
#define CMD58   58  // READ_OCR
#define ACMD41  41  // SD_SEND_OP_COND

// R1 Response bits
#define R1_IDLE_STATE   0x01

//==============================================================================
// Static Variables
//==============================================================================

static uint8_t s_is_sdhc = 0;  // 1 if SDHC/SDXC, 0 if SDSC

//==============================================================================
// Low-Level SPI Functions
//==============================================================================

static void spi_set_speed(uint32_t speed) {
    SPI_CTRL = speed;  // Speed already has bits in correct position [4:2]
}

static void spi_cs_assert(void) {
    SPI_CS_REG = 0;  // CS active low
}

static void spi_cs_deassert(void) {
    SPI_CS_REG = 1;  // CS inactive high
}

static uint8_t spi_transfer(uint8_t data) {
    SPI_DATA = data;
    // Wait while transfer in progress (SPI_STATUS_BUSY goes low when done)
    while (SPI_STATUS & SPI_STATUS_BUSY);
    return (uint8_t)(SPI_DATA & 0xFF);
}

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
// SD Card Initialization
//==============================================================================

int sd_init(void) {
    uint8_t r1;
    int retry;

    // Reset card type
    s_is_sdhc = 0;

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
        return -1;  // Init failed
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
                    return -2;  // Timeout
                }
            } while (r1 != 0x00);

            // Read OCR to check CCS bit
            if (sd_send_cmd(CMD58, 0) == 0) {
                for (int i = 0; i < 4; i++) {
                    ocr[i] = spi_transfer(0xFF);
                }
                // Check CCS bit (bit 30 of OCR)
                if (ocr[0] & 0x40) {
                    s_is_sdhc = 1;  // SDHC/SDXC card
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
                return -3;  // Card type not supported
            }
        } while (r1 != 0x00);

        s_is_sdhc = 0;  // SDSC card
    }

    // Deassert CS
    spi_cs_deassert();

    // Increase speed to 12.5 MHz for data transfers
    spi_set_speed(SPI_CLK_12MHZ);

    return 0;  // Success
}

//==============================================================================
// SD Card Data Transfer
//==============================================================================

int sd_read_sectors(uint8_t *buffer, uint32_t sector, uint32_t count) {
    uint8_t r1;

    // Read sectors one at a time
    for (uint32_t i = 0; i < count; i++) {
        uint32_t addr = sector + i;

        // For SDSC cards, sector address is byte address
        if (!s_is_sdhc) {
            addr <<= 9;  // Convert to byte address
        }

        spi_cs_assert();

        // Send CMD17 (READ_SINGLE_BLOCK)
        r1 = sd_send_cmd(CMD17, addr);
        if (r1 != 0x00) {
            spi_cs_deassert();
            return -1;  // Read command failed
        }

        // Wait for data token (0xFE)
        uint16_t timeout = 0xFFFF;
        while (spi_transfer(0xFF) != 0xFE) {
            if (--timeout == 0) {
                spi_cs_deassert();
                return -2;  // Timeout waiting for data
            }
        }

        // Read 512 bytes
        for (uint16_t j = 0; j < 512; j++) {
            buffer[i * 512 + j] = spi_transfer(0xFF);
        }

        // Read CRC (2 bytes) - ignored
        spi_transfer(0xFF);
        spi_transfer(0xFF);

        spi_cs_deassert();

        // Small delay between sectors
        for (volatile int d = 0; d < 100; d++);
    }

    return 0;  // Success
}
