//==============================================================================
// Olimex iCE40HX8K-EVB RISC-V Platform
// spi_test.c - SPI Master Peripheral Test
//
// Copyright (c) October 2025 Michael Wolak
// Email: mikewolak@gmail.com, mike@epromfoundry.com
//
// NOT FOR COMMERCIAL USE
// Educational and research purposes only
//==============================================================================

#include <stdint.h>

// UART MMIO Registers
#define UART_TX_DATA   (*(volatile uint32_t*)0x80000000)
#define UART_TX_STATUS (*(volatile uint32_t*)0x80000004)
#define UART_RX_DATA   (*(volatile uint32_t*)0x80000008)
#define UART_RX_STATUS (*(volatile uint32_t*)0x8000000C)

// SPI MMIO Registers
#define SPI_CTRL   (*(volatile uint32_t*)0x80000050)
#define SPI_DATA   (*(volatile uint32_t*)0x80000054)
#define SPI_STATUS (*(volatile uint32_t*)0x80000058)
#define SPI_CS     (*(volatile uint32_t*)0x8000005C)

// SPI Status bits
#define SPI_STATUS_BUSY (1 << 0)
#define SPI_STATUS_DONE (1 << 1)

// SPI Clock divider values
#define SPI_CLK_50MHZ   (0 << 2)  // 000 = 50 MHz
#define SPI_CLK_25MHZ   (1 << 2)  // 001 = 25 MHz
#define SPI_CLK_12MHZ   (2 << 2)  // 010 = 12.5 MHz
#define SPI_CLK_6MHZ    (3 << 2)  // 011 = 6.25 MHz
#define SPI_CLK_3MHZ    (4 << 2)  // 100 = 3.125 MHz
#define SPI_CLK_1MHZ    (5 << 2)  // 101 = 1.562 MHz
#define SPI_CLK_781KHZ  (6 << 2)  // 110 = 781 kHz
#define SPI_CLK_390KHZ  (7 << 2)  // 111 = 390 kHz

// UART functions
void uart_putc(char c) {
    while (UART_TX_STATUS & 1);  // Wait while busy
    UART_TX_DATA = c;
}

void uart_puts(const char *s) {
    while (*s) {
        uart_putc(*s++);
    }
}

void uart_puthex(uint32_t val, int digits) {
    for (int i = (digits - 1) * 4; i >= 0; i -= 4) {
        uint32_t nibble = (val >> i) & 0xF;
        uart_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
    }
}

// SPI functions
void spi_init(uint32_t clk_div) {
    // Configure: Mode 0 (CPOL=0, CPHA=0), specified clock divider
    SPI_CTRL = clk_div | (0 << 1) | (0 << 0);

    // CS inactive (high)
    SPI_CS = 1;
}

uint8_t spi_transfer(uint8_t data) {
    // Wait if busy
    while (SPI_STATUS & SPI_STATUS_BUSY);

    // Initiate transfer
    SPI_DATA = data;

    // Wait for completion
    while (SPI_STATUS & SPI_STATUS_BUSY);

    // Read status to clear DONE flag
    uint32_t status = SPI_STATUS;

    // Read received data
    return (uint8_t)SPI_DATA;
}

// Loopback test (connect MOSI to MISO with jumper wire)
void spi_loopback_test(void) {
    uart_puts("\n=== SPI Loopback Test ===\n");
    uart_puts("Connect MOSI (B1) to MISO (C1) with jumper wire\n\n");

    uint8_t test_patterns[] = {0x00, 0xFF, 0xAA, 0x55, 0x12, 0x34, 0x56, 0x78};
    int num_tests = sizeof(test_patterns) / sizeof(test_patterns[0]);
    int passed = 0;

    SPI_CS = 0;  // Assert CS

    for (int i = 0; i < num_tests; i++) {
        uint8_t tx = test_patterns[i];
        uint8_t rx = spi_transfer(tx);

        uart_puts("TX: 0x");
        uart_puthex(tx, 2);
        uart_puts(" -> RX: 0x");
        uart_puthex(rx, 2);

        if (tx == rx) {
            uart_puts(" [PASS]\n");
            passed++;
        } else {
            uart_puts(" [FAIL]\n");
        }
    }

    SPI_CS = 1;  // Deassert CS

    uart_puts("\nResults: ");
    uart_puthex(passed, 1);
    uart_puts("/");
    uart_puthex(num_tests, 1);
    uart_puts(" passed\n");
}

// Speed test at different clock rates
void spi_speed_test(void) {
    uart_puts("\n=== SPI Speed Test ===\n");
    uart_puts("Testing all clock speeds (100 bytes each)\n\n");

    const char *speed_names[] = {
        "50.0 MHz", "25.0 MHz", "12.5 MHz", "6.25 MHz",
        "3.125 MHz", "1.562 MHz", "781 kHz", "390 kHz"
    };

    uint32_t speeds[] = {
        SPI_CLK_50MHZ, SPI_CLK_25MHZ, SPI_CLK_12MHZ, SPI_CLK_6MHZ,
        SPI_CLK_3MHZ, SPI_CLK_1MHZ, SPI_CLK_781KHZ, SPI_CLK_390KHZ
    };

    for (int i = 0; i < 8; i++) {
        uart_puts("Testing ");
        uart_puts(speed_names[i]);
        uart_puts("... ");

        // Configure SPI for this speed
        spi_init(speeds[i]);

        // Transfer 100 bytes
        SPI_CS = 0;
        for (int j = 0; j < 100; j++) {
            spi_transfer(j & 0xFF);
        }
        SPI_CS = 1;

        uart_puts("OK\n");
    }
}

// Register dump
void spi_register_dump(void) {
    uart_puts("\n=== SPI Register Dump ===\n");

    uart_puts("SPI_CTRL   (0x80000050): 0x");
    uart_puthex(SPI_CTRL, 8);
    uart_puts("\n");

    uart_puts("SPI_DATA   (0x80000054): 0x");
    uart_puthex(SPI_DATA, 8);
    uart_puts("\n");

    uart_puts("SPI_STATUS (0x80000058): 0x");
    uart_puthex(SPI_STATUS, 8);
    uart_puts("\n");

    uart_puts("SPI_CS     (0x8000005C): 0x");
    uart_puthex(SPI_CS, 8);
    uart_puts("\n");
}

// Main function
int main(void) {
    uart_puts("\n");
    uart_puts("========================================\n");
    uart_puts("  SPI Master Peripheral Test\n");
    uart_puts("  Olimex iCE40HX8K-EVB PicoRV32\n");
    uart_puts("========================================\n");

    // Initialize SPI at safe speed (390 kHz)
    uart_puts("\nInitializing SPI (390 kHz, Mode 0)...\n");
    spi_init(SPI_CLK_390KHZ);

    // Show initial register state
    spi_register_dump();

    // Test 1: Loopback test (requires jumper wire)
    spi_loopback_test();

    // Test 2: Speed test (works without jumper, but RX data meaningless)
    spi_speed_test();

    // Test 3: SD card initialization pattern (no card required, just demonstrates usage)
    uart_puts("\n=== SD Card Init Pattern ===\n");
    uart_puts("Demonstrating SD card initialization sequence\n");
    uart_puts("(No SD card required for this test)\n\n");

    // Step 1: Set to 390 kHz for initialization
    spi_init(SPI_CLK_390KHZ);
    uart_puts("1. Set clock to 390 kHz\n");

    // Step 2: Send 80 dummy clocks with CS high
    uart_puts("2. Sending 80 dummy clocks (CS high)...\n");
    SPI_CS = 1;
    for (int i = 0; i < 10; i++) {
        spi_transfer(0xFF);
    }
    uart_puts("   Done\n");

    // Step 3: Send CMD0 (GO_IDLE_STATE)
    uart_puts("3. Sending CMD0 (GO_IDLE_STATE)...\n");
    SPI_CS = 0;
    spi_transfer(0x40);  // CMD0
    spi_transfer(0x00);  // ARG[31:24]
    spi_transfer(0x00);  // ARG[23:16]
    spi_transfer(0x00);  // ARG[15:8]
    spi_transfer(0x00);  // ARG[7:0]
    uint8_t crc = spi_transfer(0x95);  // CRC (valid for CMD0)
    uart_puts("   Done\n");

    // Step 4: Read response
    uart_puts("4. Reading R1 response: 0x");
    uint8_t r1 = spi_transfer(0xFF);
    uart_puthex(r1, 2);
    uart_puts("\n   (0x01 = idle state expected with SD card)\n");
    SPI_CS = 1;

    // Step 5: Switch to high speed
    uart_puts("5. Switching to 25 MHz for data transfer...\n");
    spi_init(SPI_CLK_25MHZ);
    uart_puts("   Done\n");

    uart_puts("\n========================================\n");
    uart_puts("  All tests complete!\n");
    uart_puts("========================================\n");
    uart_puts("\nNext steps:\n");
    uart_puts("- For loopback test: Connect MOSI to MISO\n");
    uart_puts("- For SD card: Connect SD card module\n");
    uart_puts("- See hdl/SPI_MASTER_README.md for details\n\n");

    // Infinite loop
    while (1) {
        __asm__ volatile ("nop");
    }

    return 0;
}
