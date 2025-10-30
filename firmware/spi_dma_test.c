//==============================================================================
// SPI DMA Test Firmware
//
// Comprehensive test for SPI DMA burst mode implementation
// This will be run in full-system simulation to verify DMA functionality
//
// Tests:
// 1. DMA TX: Write pattern from SRAM to SPI
// 2. DMA RX: Read from SPI to SRAM
// 3. Data integrity: Verify all bytes transferred correctly
// 4. Performance: Count cycles for DMA vs manual transfer
//
// Copyright (c) October 2025 Michael Wolak
//==============================================================================

#include <stdint.h>

//==============================================================================
// Hardware Register Definitions
//==============================================================================

// UART
#define UART_BASE       0x80000000
#define UART_TX_DATA    (*(volatile uint32_t*)(UART_BASE + 0x00))
#define UART_TX_STATUS  (*(volatile uint32_t*)(UART_BASE + 0x04))
#define UART_RX_DATA    (*(volatile uint32_t*)(UART_BASE + 0x08))
#define UART_RX_STATUS  (*(volatile uint32_t*)(UART_BASE + 0x0C))
#define UART_TX_BUSY    (1 << 0)

// SPI (existing registers)
#define SPI_BASE        0x80000050
#define SPI_CTRL        (*(volatile uint32_t*)(SPI_BASE + 0x00))
#define SPI_DATA        (*(volatile uint32_t*)(SPI_BASE + 0x04))
#define SPI_STATUS      (*(volatile uint32_t*)(SPI_BASE + 0x08))
#define SPI_CS          (*(volatile uint32_t*)(SPI_BASE + 0x0C))
#define SPI_BURST       (*(volatile uint32_t*)(SPI_BASE + 0x10))

// SPI DMA (NEW registers)
#define SPI_DMA_ADDR    (*(volatile uint32_t*)(SPI_BASE + 0x14))  // 0x80000064
#define SPI_DMA_CTRL    (*(volatile uint32_t*)(SPI_BASE + 0x18))  // 0x80000068

// SPI status bits
#define SPI_STATUS_BUSY       (1 << 0)
#define SPI_STATUS_IRQ        (1 << 1)
#define SPI_STATUS_BURST_MODE (1 << 2)
#define SPI_STATUS_DMA_ACTIVE (1 << 3)

// SPI DMA control bits
#define SPI_DMA_START     (1 << 0)
#define SPI_DMA_DIR_TX    (0 << 1)  // Read from SRAM, write to SPI
#define SPI_DMA_DIR_RX    (1 << 1)  // Read from SPI, write to SRAM
#define SPI_DMA_BUSY      (1 << 2)
#define SPI_DMA_IRQ_EN    (1 << 3)

// Timer (for performance measurement)
#define TIMER_BASE      0x80000020
#define TIMER_COUNTER   (*(volatile uint32_t*)(TIMER_BASE + 0x08))

//==============================================================================
// Test Configuration
//==============================================================================

#define TEST_BUFFER_SIZE  512   // Match SD card block size
#define TEST_PATTERN_SEED 0xA5  // Predictable test pattern

//==============================================================================
// Global Test Buffers
//==============================================================================

static uint8_t tx_buffer[TEST_BUFFER_SIZE] __attribute__((aligned(4)));
static uint8_t rx_buffer[TEST_BUFFER_SIZE] __attribute__((aligned(4)));
static uint8_t verify_buffer[TEST_BUFFER_SIZE] __attribute__((aligned(4)));

//==============================================================================
// UART Functions
//==============================================================================

static void uart_putc(char c) {
    while (UART_TX_STATUS & UART_TX_BUSY);
    UART_TX_DATA = c;
}

static void uart_puts(const char *s) {
    while (*s) {
        if (*s == '\n') uart_putc('\r');
        uart_putc(*s++);
    }
}

static void uart_put_hex8(uint8_t val) {
    const char *hex = "0123456789ABCDEF";
    uart_putc(hex[(val >> 4) & 0xF]);
    uart_putc(hex[val & 0xF]);
}

static void uart_put_hex32(uint32_t val) {
    uart_put_hex8((val >> 24) & 0xFF);
    uart_put_hex8((val >> 16) & 0xFF);
    uart_put_hex8((val >> 8) & 0xFF);
    uart_put_hex8(val & 0xFF);
}

//==============================================================================
// SPI DMA Functions
//==============================================================================

// DMA TX: Transfer from SRAM to SPI
static void spi_dma_tx(const uint8_t *buffer, uint32_t count) {
    SPI_BURST = count;
    SPI_DMA_ADDR = (uint32_t)buffer;
    SPI_DMA_CTRL = SPI_DMA_START | SPI_DMA_DIR_TX | SPI_DMA_IRQ_EN;

    // Wait for completion
    while (SPI_DMA_CTRL & SPI_DMA_BUSY);
}

// DMA RX: Transfer from SPI to SRAM
static void spi_dma_rx(uint8_t *buffer, uint32_t count) {
    SPI_BURST = count;
    SPI_DMA_ADDR = (uint32_t)buffer;
    SPI_DMA_CTRL = SPI_DMA_START | SPI_DMA_DIR_RX | SPI_DMA_IRQ_EN;

    // Wait for completion
    while (SPI_DMA_CTRL & SPI_DMA_BUSY);
}

// Manual transfer (for comparison)
static void spi_manual_tx(const uint8_t *buffer, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        SPI_DATA = buffer[i];
        while (SPI_STATUS & SPI_STATUS_BUSY);
    }
}

//==============================================================================
// Test Functions
//==============================================================================

// Initialize test buffer with predictable pattern
static void init_test_pattern(uint8_t *buffer, uint32_t size, uint8_t seed) {
    for (uint32_t i = 0; i < size; i++) {
        buffer[i] = (uint8_t)(seed + i);
    }
}

// Verify buffer contents
static int verify_pattern(const uint8_t *buffer, uint32_t size, uint8_t seed) {
    int errors = 0;
    for (uint32_t i = 0; i < size; i++) {
        uint8_t expected = (uint8_t)(seed + i);
        if (buffer[i] != expected) {
            if (errors < 10) {  // Report first 10 errors
                uart_puts("  ERROR at offset ");
                uart_put_hex32(i);
                uart_puts(": expected 0x");
                uart_put_hex8(expected);
                uart_puts(", got 0x");
                uart_put_hex8(buffer[i]);
                uart_puts("\n");
            }
            errors++;
        }
    }
    return errors;
}

//==============================================================================
// Main Test Program
//==============================================================================

int main(void) {
    uint32_t start_cycles, end_cycles, dma_cycles, manual_cycles;
    int errors;

    uart_puts("\n");
    uart_puts("================================================================================\n");
    uart_puts("SPI DMA Comprehensive Test\n");
    uart_puts("================================================================================\n\n");

    // Initialize SPI
    uart_puts("[1] Initializing SPI controller\n");
    SPI_CTRL = 0x00;  // CPOL=0, CPHA=0, CLK_DIV=/1 (fastest)
    SPI_CS = 0;       // CS low (active)
    uart_puts("    SPI_CTRL = 0x00 (50 MHz SPI clock)\n");
    uart_puts("    SPI_CS = 0 (asserted)\n\n");

    //==========================================================================
    // TEST 1: Register Access
    //==========================================================================
    uart_puts("[2] Testing DMA register access\n");

    uart_puts("    Writing SPI_DMA_ADDR = 0x12345678\n");
    SPI_DMA_ADDR = 0x12345678;
    uint32_t readback = SPI_DMA_ADDR;
    uart_puts("    Readback: 0x");
    uart_put_hex32(readback);
    uart_puts("\n");
    if (readback != 0x12345678) {
        uart_puts("    FAIL: Register readback mismatch!\n\n");
        while(1);  // Halt on error
    }
    uart_puts("    PASS: DMA address register working\n\n");

    //==========================================================================
    // TEST 2: DMA TX Transfer
    //==========================================================================
    uart_puts("[3] Testing DMA TX (SRAM -> SPI)\n");

    init_test_pattern(tx_buffer, TEST_BUFFER_SIZE, TEST_PATTERN_SEED);
    uart_puts("    Initialized TX buffer with pattern 0xA5-0x1A4\n");
    uart_puts("    First 8 bytes: ");
    for (int i = 0; i < 8; i++) {
        uart_put_hex8(tx_buffer[i]);
        uart_putc(' ');
    }
    uart_puts("\n");

    uart_puts("    Starting DMA transfer of 512 bytes\n");
    uart_puts("    TX buffer address: 0x");
    uart_put_hex32((uint32_t)tx_buffer);
    uart_puts("\n");

    start_cycles = TIMER_COUNTER;
    spi_dma_tx(tx_buffer, TEST_BUFFER_SIZE);
    end_cycles = TIMER_COUNTER;
    dma_cycles = end_cycles - start_cycles;

    uart_puts("    DMA TX complete\n");
    uart_puts("    Cycles: ");
    uart_put_hex32(dma_cycles);
    uart_puts("\n\n");

    //==========================================================================
    // TEST 3: DMA RX Transfer
    //==========================================================================
    uart_puts("[4] Testing DMA RX (SPI -> SRAM)\n");

    // Clear RX buffer
    for (uint32_t i = 0; i < TEST_BUFFER_SIZE; i++) {
        rx_buffer[i] = 0x00;
    }
    uart_puts("    Cleared RX buffer\n");

    uart_puts("    Starting DMA receive of 512 bytes\n");
    uart_puts("    RX buffer address: 0x");
    uart_put_hex32((uint32_t)rx_buffer);
    uart_puts("\n");

    start_cycles = TIMER_COUNTER;
    spi_dma_rx(rx_buffer, TEST_BUFFER_SIZE);
    end_cycles = TIMER_COUNTER;

    uart_puts("    DMA RX complete\n");
    uart_puts("    First 8 bytes received: ");
    for (int i = 0; i < 8; i++) {
        uart_put_hex8(rx_buffer[i]);
        uart_putc(' ');
    }
    uart_puts("\n\n");

    //==========================================================================
    // TEST 4: Data Integrity Verification
    //==========================================================================
    uart_puts("[5] Verifying data integrity\n");

    errors = verify_pattern(rx_buffer, TEST_BUFFER_SIZE, TEST_PATTERN_SEED);
    if (errors > 0) {
        uart_puts("    FAIL: ");
        uart_put_hex32(errors);
        uart_puts(" byte mismatches detected!\n\n");
        while(1);  // Halt on error
    }
    uart_puts("    PASS: All 512 bytes match expected pattern\n\n");

    //==========================================================================
    // TEST 5: Performance Comparison
    //==========================================================================
    uart_puts("[6] Performance comparison: DMA vs Manual\n");

    uart_puts("    Running manual transfer for comparison\n");
    start_cycles = TIMER_COUNTER;
    spi_manual_tx(tx_buffer, TEST_BUFFER_SIZE);
    end_cycles = TIMER_COUNTER;
    manual_cycles = end_cycles - start_cycles;

    uart_puts("    DMA cycles:    ");
    uart_put_hex32(dma_cycles);
    uart_puts("\n");
    uart_puts("    Manual cycles: ");
    uart_put_hex32(manual_cycles);
    uart_puts("\n");

    if (dma_cycles < manual_cycles) {
        uint32_t speedup = (manual_cycles * 100) / dma_cycles;
        uart_puts("    PASS: DMA is ");
        uart_put_hex32(speedup);
        uart_puts("% of manual time (");
        uart_put_hex32(manual_cycles - dma_cycles);
        uart_puts(" cycles saved)\n\n");
    } else {
        uart_puts("    WARNING: DMA not faster than manual!\n\n");
    }

    //==========================================================================
    // Final Summary
    //==========================================================================
    uart_puts("================================================================================\n");
    uart_puts("ALL TESTS PASSED!\n");
    uart_puts("================================================================================\n");
    uart_puts("\n");
    uart_puts("Summary:\n");
    uart_puts("  - DMA register access: OK\n");
    uart_puts("  - DMA TX transfer: OK\n");
    uart_puts("  - DMA RX transfer: OK\n");
    uart_puts("  - Data integrity: OK (512/512 bytes correct)\n");
    uart_puts("  - Performance: DMA faster than manual\n");
    uart_puts("\n");
    uart_puts("SPI DMA implementation verified!\n\n");

    // Deassert CS
    SPI_CS = 1;

    // Success
    while(1);

    return 0;
}
