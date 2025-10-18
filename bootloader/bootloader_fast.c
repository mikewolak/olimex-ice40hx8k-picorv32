//==============================================================================
// Olimex iCE40HX8K-EVB RISC-V Platform - FAST Bootloader
// bootloader_fast.c - FAST streaming bootloader (NO chunking)
//
// Copyright (c) October 2025 Michael Wolak
// Email: mikewolak@gmail.com, mike@epromfoundry.com
//
// NOT FOR COMMERCIAL USE
// Educational and research purposes only
//==============================================================================

/*
 * FAST Bootloader - Runs from BRAM at 0x40000
 *
 * Memory Layout:
 *   0x00000000 - 0x0003FFFF : Main firmware space (256KB)
 *   0x00040000 - 0x00041FFF : This bootloader (8KB ROM)
 *   0x00042000 - 0x0007FFFF : Heap/Stack (~248KB)
 *
 * FAST Protocol (matches hexedit_fast.c and fw_upload_fast.c):
 *   1. PC sends 'R' (Ready) → Bootloader sends 'A'
 *   2. PC sends 4-byte size (little-endian) → Bootloader sends 'B'
 *   3. PC streams ALL data continuously (NO chunking, NO per-chunk ACKs!)
 *   4. PC sends 'C' + 4-byte CRC → Bootloader calculates CRC
 *   5. Bootloader sends 'C' + 4-byte calculated CRC
 *   6. Bootloader jumps to 0x0
 *
 * IMPORTANT: This bootloader uses FAST streaming protocol that is ONLY
 * compatible with fw_upload_fast. It is NOT compatible with fw_upload.
 * Use the correct pairing:
 *   bootloader_fast.elf <-> fw_upload_fast (FAST streaming, NO chunking)
 *   bootloader.elf      <-> fw_upload (standard chunked protocol)
 *
 * CRC32 is calculated over data only (not size bytes)
 */

#include <stdint.h>

// MMIO Addresses
#define UART_TX_DATA   (*(volatile uint32_t *)0x80000000)
#define UART_TX_STATUS (*(volatile uint32_t *)0x80000004)
#define UART_RX_DATA   (*(volatile uint32_t *)0x80000008)
#define UART_RX_STATUS (*(volatile uint32_t *)0x8000000C)
#define LED_CONTROL    (*(volatile uint32_t *)0x80000010)
#define BUTTON_STATUS  (*(volatile uint32_t *)0x80000018)

// Target firmware location
#define FIRMWARE_BASE  0x00000000
#define MAX_FIRMWARE_SIZE (256 * 1024)  // 256KB max

// External assembly function
extern void jump_to_firmware(uint32_t addr);

//=============================================================================
// UART Functions
//=============================================================================

static void uart_putc(uint8_t c) {
    while (UART_TX_STATUS & 1);  // Wait while busy
    UART_TX_DATA = c;
}

static uint8_t uart_getc(void) {
    while (!(UART_RX_STATUS & 1));  // Wait until data available
    return UART_RX_DATA & 0xFF;
}

//=============================================================================
// CRC32 Calculation (matches hexedit_fast.c and fw_upload_fast.c)
//=============================================================================

static uint32_t crc32_table[256];

static void crc32_init(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320 : 0);
        }
        crc32_table[i] = crc;
    }
}

// Calculate CRC32 of a memory block (post-receive)
static uint32_t calculate_crc32(uint32_t start_addr, uint32_t end_addr) {
    uint32_t crc = 0xFFFFFFFF;
    for (uint32_t addr = start_addr; addr <= end_addr; addr++) {
        uint8_t byte = *((uint8_t *)addr);
        crc = (crc >> 8) ^ crc32_table[(crc ^ byte) & 0xFF];
    }
    return ~crc;
}

//=============================================================================
// Main Bootloader - Implements FAST streaming protocol
//=============================================================================

void bootloader_main(void) {
    uint8_t *firmware = (uint8_t *)FIRMWARE_BASE;
    uint32_t packet_size = 0;
    uint32_t bytes_received = 0;
    uint32_t expected_crc;
    uint32_t calculated_crc;

    // Initialize CRC32 lookup table
    crc32_init();

    // LED pattern: LED1 on = waiting for upload
    LED_CONTROL = 0x01;

    // Step 1: Wait for 'R' (Ready) command
    while (1) {
        uint8_t cmd = uart_getc();
        if (cmd == 'R' || cmd == 'r') {
            break;
        }
    }

    // Step 2: Send ACK 'A' for Ready
    uart_putc('A');

    // LED pattern: LED2 on = downloading
    LED_CONTROL = 0x02;

    // Step 3: Receive 4-byte packet size (little-endian)
    for (int i = 0; i < 4; i++) {
        uint8_t byte = uart_getc();
        packet_size |= ((uint32_t)byte) << (i * 8);
    }

    // Step 4: Send ACK 'B' for size received
    uart_putc('B');

    // Validate size
    if (packet_size == 0 || packet_size > MAX_FIRMWARE_SIZE) {
        LED_CONTROL = 0x00;  // Turn off LEDs = error
        while (1);  // Halt on error
    }

    // Step 5: STREAM ALL DATA (no chunking, no ACKs!)
    // This is the key difference from the standard bootloader
    while (bytes_received < packet_size) {
        firmware[bytes_received] = uart_getc();
        bytes_received++;

        // Toggle LEDs to show progress (visual feedback only)
        if ((bytes_received & 0x3FF) == 0) {  // Every 1024 bytes
            if ((bytes_received >> 10) & 1) {
                LED_CONTROL = 0x03;  // Both LEDs
            } else {
                LED_CONTROL = 0x02;  // LED2 only
            }
        }
    }

    // Step 6: Calculate CRC32 of received data (post-receive)
    calculated_crc = calculate_crc32(FIRMWARE_BASE, FIRMWARE_BASE + packet_size - 1);

    // Step 7: Wait for 'C' (CRC command)
    uint8_t crc_cmd = uart_getc();
    if (crc_cmd != 'C') {
        LED_CONTROL = 0x00;  // Error
        while (1);
    }

    // Step 8: Receive 4-byte expected CRC (little-endian)
    expected_crc = 0;
    for (int i = 0; i < 4; i++) {
        uint8_t byte = uart_getc();
        expected_crc |= ((uint32_t)byte) << (i * 8);
    }

    // Step 9: Send 'C' + calculated CRC back to host
    uart_putc('C');

    // Send calculated CRC (little-endian)
    uart_putc((calculated_crc >> 0) & 0xFF);
    uart_putc((calculated_crc >> 8) & 0xFF);
    uart_putc((calculated_crc >> 16) & 0xFF);
    uart_putc((calculated_crc >> 24) & 0xFF);

    // Step 10: Verify CRC match
    if (calculated_crc != expected_crc) {
        LED_CONTROL = 0x00;  // Error - CRC mismatch
        while (1);  // Halt on CRC error
    }

    // Success! Turn off LEDs before jumping
    LED_CONTROL = 0x00;

    // Step 11: Jump to firmware at 0x0
    jump_to_firmware(FIRMWARE_BASE);

    // Should never return
    while (1);
}
