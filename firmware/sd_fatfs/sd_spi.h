//==============================================================================
// SD Card SPI Driver for PicoRV32
// Low-level SD card interface using SPI master peripheral
//==============================================================================

#ifndef SD_SPI_H
#define SD_SPI_H

#include <stdint.h>
#include "hardware.h"

//==============================================================================
// SD Card Commands
//==============================================================================

#define CMD0    0   // GO_IDLE_STATE
#define CMD1    1   // SEND_OP_COND (MMC)
#define CMD8    8   // SEND_IF_COND
#define CMD9    9   // SEND_CSD
#define CMD10   10  // SEND_CID
#define CMD12   12  // STOP_TRANSMISSION
#define CMD16   16  // SET_BLOCKLEN
#define CMD17   17  // READ_SINGLE_BLOCK
#define CMD18   18  // READ_MULTIPLE_BLOCK
#define CMD23   23  // SET_BLOCK_COUNT (MMC)
#define CMD24   24  // WRITE_BLOCK
#define CMD25   25  // WRITE_MULTIPLE_BLOCK
#define CMD32   32  // ERASE_WR_BLK_START
#define CMD33   33  // ERASE_WR_BLK_END
#define CMD38   38  // ERASE
#define CMD55   55  // APP_CMD
#define CMD58   58  // READ_OCR
#define ACMD13  13  // SD_STATUS (SDC)
#define ACMD23  23  // SET_WR_BLK_ERASE_COUNT (SDC)
#define ACMD41  41  // SD_SEND_OP_COND (SDC)

//==============================================================================
// SD Card Response Types
//==============================================================================

#define R1_IDLE_STATE           (1 << 0)
#define R1_ERASE_RESET          (1 << 1)
#define R1_ILLEGAL_COMMAND      (1 << 2)
#define R1_COM_CRC_ERROR        (1 << 3)
#define R1_ERASE_SEQUENCE_ERROR (1 << 4)
#define R1_ADDRESS_ERROR        (1 << 5)
#define R1_PARAMETER_ERROR      (1 << 6)

//==============================================================================
// SD Card Types
//==============================================================================

typedef enum {
    CARD_TYPE_UNKNOWN = 0,
    CARD_TYPE_SD1,      // SD v1.x (SDSC)
    CARD_TYPE_SD2,      // SD v2.0 (SDSC)
    CARD_TYPE_SDHC      // SD v2.0 (SDHC/SDXC)
} sd_card_type_t;

//==============================================================================
// Error Codes
//==============================================================================

typedef enum {
    SD_OK = 0,
    SD_ERROR_INIT,
    SD_ERROR_TIMEOUT,
    SD_ERROR_READ,
    SD_ERROR_WRITE,
    SD_ERROR_CRC,
    SD_ERROR_NOT_READY,
    SD_ERROR_CARD_TYPE
} sd_error_t;

//==============================================================================
// SD Card Structures
//==============================================================================

// CID - Card Identification Register
typedef struct {
    uint8_t  mid;       // Manufacturer ID
    uint8_t  oid[2];    // OEM/Application ID
    uint8_t  pnm[5];    // Product name
    uint8_t  prv;       // Product revision
    uint32_t psn;       // Product serial number
    uint16_t mdt;       // Manufacturing date
    uint8_t  crc;       // CRC7 checksum
} sd_cid_t;

// CSD - Card Specific Data Register
typedef struct {
    uint8_t  csd_structure;
    uint8_t  tran_speed;
    uint16_t ccc;       // Card command classes
    uint8_t  read_bl_len;
    uint32_t c_size;
    uint8_t  wp;        // Write protect
} sd_csd_t;

//==============================================================================
// Function Prototypes
//==============================================================================

// Initialization
void sd_spi_init(void);
uint8_t sd_init(void);

// Configuration
void sd_set_speed(uint32_t speed);

// Card Information
sd_card_type_t sd_get_card_type(void);
uint32_t sd_get_sector_count(void);
uint8_t sd_read_cid(sd_cid_t *cid);
uint8_t sd_read_csd(sd_csd_t *csd);

// Data Transfer
uint8_t sd_read_block(uint32_t sector, uint8_t *buffer);
uint8_t sd_write_block(uint32_t sector, const uint8_t *buffer);

// Utility
const char* sd_get_error_string(uint8_t error);

#endif // SD_SPI_H
