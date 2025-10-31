//==============================================================================
// SD Card Bootloader for PicoRV32
//
// Reads bootloader from SD card sectors 1-376 (192 KB) and loads to RAM at 0x0
// Then jumps to 0x0 to execute the loaded bootloader.
//
// This replaces the UART bootloader in the HDL bitstream, enabling SD card boot
// without requiring a host PC connection.
//
// Copyright (c) October 2025 Michael Wolak
// Email: mikewolak@gmail.com, mike@epromfoundry.com
//==============================================================================

#include <stdint.h>
#include <stdbool.h>

//==============================================================================
// Hardware Registers
//==============================================================================

#define LED_BASE    0x80000010
#define LED_REG     (*(volatile uint32_t*)LED_BASE)

#define UART_BASE   0x80000000
#define UART_DATA   (*(volatile uint32_t*)(UART_BASE + 0x00))
#define UART_STATUS (*(volatile uint32_t*)(UART_BASE + 0x04))
#define UART_TXRDY  (1 << 0)
#define UART_RXRDY  (1 << 1)

//==============================================================================
// UART Functions (for status output)
//==============================================================================

static void uart_putc(char c) {
    while (!(UART_STATUS & UART_TXRDY));
    UART_DATA = c;
}

static void uart_puts(const char *s) {
    while (*s) {
        if (*s == '\n') uart_putc('\r');
        uart_putc(*s++);
    }
}

static void uart_puthex(uint32_t val, int digits) {
    for (int i = (digits-1); i >= 0; i--) {
        uint32_t d = (val >> (i*4)) & 0xF;
        uart_putc(d + (d < 10 ? '0' : 'A' - 10));
    }
}

static void uart_putdec(uint32_t val) {
    if (val == 0) {
        uart_putc('0');
        return;
    }

    char buf[12];
    int pos = 0;
    while (val > 0) {
        buf[pos++] = '0' + (val % 10);
        val /= 10;
    }
    while (pos > 0) {
        uart_putc(buf[--pos]);
    }
}

//==============================================================================
// SD Card / SPI Interface
//==============================================================================

// We'll use the existing SD/SPI drivers from sd_fatfs
// For now, include the necessary headers

// Include minimal SD/SPI driver (no FatFS dependency)
#include "sd_spi_minimal.h"

//==============================================================================
// Memory Configuration
//==============================================================================

#define BOOT_LOAD_ADDR      0x00000000  // Load bootloader to RAM start
#define BOOT_START_SECTOR   1           // Start reading from sector 1 (after MBR)
#define BOOT_SECTOR_COUNT   375         // Read 375 sectors = 192000 bytes (~192 KB)
#define SECTOR_SIZE         512

//==============================================================================
// Main Bootloader
//==============================================================================

void main(void) {
    uint8_t *load_addr = (uint8_t *)BOOT_LOAD_ADDR;
    int result;

    // LED: Solid during boot
    LED_REG = 0x01;

    // Print banner
    uart_puts("\n");
    uart_puts("========================================\n");
    uart_puts("PicoRV32 SD Card Bootloader v1.0\n");
    uart_puts("========================================\n");
    uart_puts("Loading bootloader from SD card...\n");
    uart_puts("\n");

    // Initialize SD card
    uart_puts("Initializing SD card...\n");
    result = sd_init();
    if (result != 0) {
        uart_puts("ERROR: SD card init failed (code ");
        uart_putdec(result);
        uart_puts(")\n");
        uart_puts("Cannot boot without SD card!\n");
        LED_REG = 0x00;
        while (1) {
            // Blink LED to indicate error
            for (volatile int i = 0; i < 500000; i++);
            LED_REG ^= 0x01;
        }
    }
    uart_puts("  Status: OK\n");
    uart_puts("\n");

    // Read bootloader from SD card
    uart_puts("Reading bootloader from SD card...\n");
    uart_puts("  Start sector: ");
    uart_putdec(BOOT_START_SECTOR);
    uart_puts("\n");
    uart_puts("  Sector count: ");
    uart_putdec(BOOT_SECTOR_COUNT);
    uart_puts(" (");
    uart_putdec(BOOT_SECTOR_COUNT * SECTOR_SIZE);
    uart_puts(" bytes)\n");
    uart_puts("  Load address: 0x");
    uart_puthex(BOOT_LOAD_ADDR, 8);
    uart_puts("\n");
    uart_puts("\n");

    uart_puts("Loading to RAM");

    // Read sectors in chunks for progress indication
    #define CHUNK_SIZE 64  // Read 64 sectors at a time (32 KB)
    uint32_t sectors_read = 0;

    while (sectors_read < BOOT_SECTOR_COUNT) {
        uint32_t chunk = BOOT_SECTOR_COUNT - sectors_read;
        if (chunk > CHUNK_SIZE) chunk = CHUNK_SIZE;

        result = sd_read_sectors(load_addr + (sectors_read * SECTOR_SIZE),
                                BOOT_START_SECTOR + sectors_read,
                                chunk);

        if (result != 0) {
            uart_puts("\n");
            uart_puts("ERROR: SD read failed at sector ");
            uart_putdec(BOOT_START_SECTOR + sectors_read);
            uart_puts(" (code ");
            uart_putdec(result);
            uart_puts(")\n");
            LED_REG = 0x00;
            while (1);
        }

        sectors_read += chunk;

        // Show progress
        uart_putc('.');
        if ((sectors_read % (CHUNK_SIZE * 4)) == 0) {
            uart_putc(' ');
            uart_putdec((sectors_read * 100) / BOOT_SECTOR_COUNT);
            uart_putc('%');
        }
    }

    uart_puts("\n");
    uart_puts("\n");
    uart_puts("========================================\n");
    uart_puts("Boot Complete!\n");
    uart_puts("========================================\n");
    uart_puts("Loaded: ");
    uart_putdec(BOOT_SECTOR_COUNT * SECTOR_SIZE);
    uart_puts(" bytes to 0x");
    uart_puthex(BOOT_LOAD_ADDR, 8);
    uart_puts("\n");
    uart_puts("Jumping to bootloader...\n");
    uart_puts("\n");

    // Brief delay for UART to flush
    for (volatile int i = 0; i < 100000; i++);

    // LED: Off before jumping
    LED_REG = 0x00;

    // Jump to loaded bootloader at 0x0
    // This is effectively a restart since we loaded code at address 0
    typedef void (*entry_func_t)(void);
    entry_func_t entry = (entry_func_t)BOOT_LOAD_ADDR;
    entry();

    // Should never get here
    while (1);
}
