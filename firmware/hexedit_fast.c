//==============================================================================
// Olimex iCE40HX8K-EVB RISC-V Platform
// hexedit_fast.c - Interactive Hex Editor with FAST Streaming Upload Protocol
//
// Copyright (c) October 2025 Michael Wolak
// Email: mikewolak@gmail.com, mike@epromfoundry.com
//
// NOT FOR COMMERCIAL USE
// Educational and research purposes only
//==============================================================================

/*
 * Interactive Hex Editor Features:
 * - Memory dump (hex and ASCII)
 * - Memory read/write
 * - Memory block copy/move
 * - FAST Streaming Upload (NO chunking, NO per-chunk ACKs)
 * - 128KB receive limit, buffer at heap-140KB
 */

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "../lib/microrl/microrl.h"
#include "../lib/incurses/curses.h"

// Hardware addresses
#define UART_TX_DATA   (*(volatile uint32_t *)0x80000000)
#define UART_TX_STATUS (*(volatile uint32_t *)0x80000004)
#define UART_RX_DATA   (*(volatile uint32_t *)0x80000008)
#define UART_RX_STATUS (*(volatile uint32_t *)0x8000000C)

// Timer registers
#define TIMER_BASE          0x80000020
#define TIMER_CR            (*(volatile uint32_t*)(TIMER_BASE + 0x00))
#define TIMER_SR            (*(volatile uint32_t*)(TIMER_BASE + 0x04))
#define TIMER_PSC           (*(volatile uint32_t*)(TIMER_BASE + 0x08))
#define TIMER_ARR           (*(volatile uint32_t*)(TIMER_BASE + 0x0C))
#define TIMER_CNT           (*(volatile uint32_t*)(TIMER_BASE + 0x10))

#define TIMER_CR_ENABLE     (1 << 0)
#define TIMER_CR_ONE_SHOT   (1 << 1)
#define TIMER_SR_UIF        (1 << 0)

// Clock state (updated by interrupt at 60 Hz)
volatile uint32_t clock_frames = 0;   // Frame counter (0-59, increments at 60 Hz)
volatile uint32_t clock_seconds = 0;  // Seconds counter (0-59)
volatile uint32_t clock_minutes = 0;  // Minutes counter (0-59)
volatile uint32_t clock_hours = 0;    // Hours counter (0-23)
volatile uint8_t clock_updated = 0;   // Flag: clock changed
volatile uint8_t clock_enabled = 0;   // Flag: clock display enabled (0=off, 1=on)

// Millisecond counter for timeouts (updated by interrupt)
volatile uint32_t millis = 0;         // Total milliseconds since start

// Memory layout (based on linker script)
// Physical memory: 0x00000000 - 0x0007FFFF (512KB SRAM)
// Application:     0x00000000 - 0x0003FFFF (256KB for code/data/bss)
// Heap:            End of BSS - 0x00042000
// Stack:           0x00042000 - 0x00080000 (grows down from 0x80000)
#define HEAP_END       0x00042000   // Heap ends here (from linker script)

// File transfer configuration
#define ZM_MAX_RECEIVE    (128 * 1024)          // 128KB max transfer
#define ZM_BUFFER_OFFSET  (140 * 1024)          // 140KB before heap end
#define ZM_BUFFER_ADDR    (HEAP_END - ZM_BUFFER_OFFSET)

//==============================================================================
// PicoRV32 Interrupt Control (custom instructions)
//==============================================================================

// Enable interrupts (clear IRQ mask)
static inline void irq_enable(void) {
    uint32_t dummy;
    __asm__ volatile (".insn r 0x0B, 6, 3, %0, %1, x0" : "=r"(dummy) : "r"(0));
}

// Disable interrupts (set IRQ mask to all 1s)
static inline void irq_disable(void) {
    uint32_t dummy;
    __asm__ volatile (".insn r 0x0B, 6, 3, %0, %1, x0" : "=r"(dummy) : "r"(~0));
}

//==============================================================================
// Forward Declarations
//==============================================================================
void timer_init(void);
uint32_t get_time_ms(void);
void execute_command(const char *cmd);
static void crc32_init(void);
static uint32_t calculate_crc32(uint32_t start_addr, uint32_t end_addr);

// Global state for pagination
static uint32_t last_dump_addr = 0;
static uint32_t last_dump_len = 0x100;  // 256 bytes

//==============================================================================
// UART Functions
//==============================================================================

void uart_putc(char c) {
    while (UART_TX_STATUS & 1);  // Wait while busy
    UART_TX_DATA = c;
}

void uart_puts(const char *s) {
    while (*s) {
        if (*s == '\n') uart_putc('\r');
        uart_putc(*s++);
    }
}

int uart_getc_available(void) {
    return UART_RX_STATUS & 1;
}

char uart_getc(void) {
    while (!uart_getc_available());
    return UART_RX_DATA & 0xFF;
}

// Flush UART RX buffer (discard all pending data)
void uart_flush_rx(void) {
    while (uart_getc_available()) {
        (void)UART_RX_DATA;  // Discard byte
    }
}

int getc_timeout(uint32_t timeout_ms) {
    uint32_t start = get_time_ms();
    while ((get_time_ms() - start) < timeout_ms) {
        if (uart_getc_available()) {
            return (int)(UART_RX_DATA & 0xFF);  // Return byte as positive int
        }
    }
    return -1;  // Timeout - returns proper -1 as int
}

//==============================================================================
// Interrupt Handler
//==============================================================================

void irq_handler(uint32_t irqs) {
    // Check if Timer interrupt (IRQ[0])
    if (irqs & (1 << 0)) {
        // CRITICAL: Clear the interrupt source FIRST
        TIMER_SR = TIMER_SR_UIF;  // Write 1 to clear

        // Update millisecond counter (60 Hz = ~16.67ms per tick)
        millis += 17;  // Approximate: 1000ms / 60Hz ≈ 16.67ms

        // Update frame counter (0-59)
        clock_frames++;
        if (clock_frames >= 60) {
            clock_frames = 0;

            // Update seconds
            clock_seconds++;
            if (clock_seconds >= 60) {
                clock_seconds = 0;

                // Update minutes
                clock_minutes++;
                if (clock_minutes >= 60) {
                    clock_minutes = 0;

                    // Update hours
                    clock_hours++;
                    if (clock_hours >= 24) {
                        clock_hours = 0;
                    }
                }
            }
        }

        clock_updated = 1;  // Signal main loop
    }
}

//==============================================================================
// Timer Functions
//==============================================================================

// Initialize timer for 60 Hz interrupts (50MHz system clock)
void timer_init(void) {
    // Stop timer if running
    TIMER_CR = 0x00000000;

    // Clear any pending interrupt
    TIMER_SR = 0x00000001;

    // Configure for 60 Hz (16.67ms period)
    // System clock: 50 MHz
    // Prescaler: 49 (divide by 50) → 1 MHz tick rate
    // Auto-reload: 16666 → 1,000,000 / 16,667 = 59.998 Hz ≈ 60 Hz
    TIMER_PSC = 49;
    TIMER_ARR = 16666;
    TIMER_CNT = 0;

    // Start timer (continuous mode, generates interrupts)
    TIMER_CR = 0x00000001;
}

// Get current time in milliseconds
uint32_t get_time_ms(void) {
    return millis;
}

//==============================================================================
// Utility Functions
//==============================================================================

// Convert hex digit to value
int hex_to_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Print hex byte
void print_hex_byte(uint8_t b) {
    const char hex[] = "0123456789ABCDEF";
    uart_putc(hex[b >> 4]);
    uart_putc(hex[b & 0x0F]);
}

// Print hex word (32-bit)
void print_hex_word(uint32_t w) {
    print_hex_byte((w >> 24) & 0xFF);
    print_hex_byte((w >> 16) & 0xFF);
    print_hex_byte((w >> 8) & 0xFF);
    print_hex_byte(w & 0xFF);
}

// Print decimal number
void print_dec(uint32_t n) {
    char buf[12];
    int i = 0;

    if (n == 0) {
        uart_putc('0');
        return;
    }

    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }

    while (i > 0) {
        uart_putc(buf[--i]);
    }
}

//==============================================================================
// Memory Operations
//==============================================================================

void cmd_dump(uint32_t addr, uint32_t len) {
    uint8_t *ptr = (uint8_t *)addr;

    for (uint32_t i = 0; i < len; i += 16) {
        // Print address
        print_hex_word(addr + i);
        uart_puts(": ");

        // Print hex bytes
        for (int j = 0; j < 16 && (i + j) < len; j++) {
            print_hex_byte(ptr[i + j]);
            uart_putc(' ');
        }

        // Padding for short lines
        for (int j = len - i; j < 16 && j >= 0; j++) {
            uart_puts("   ");
        }

        uart_puts(" |");

        // Print ASCII
        for (int j = 0; j < 16 && (i + j) < len; j++) {
            char c = ptr[i + j];
            uart_putc((c >= 32 && c < 127) ? c : '.');
        }

        uart_puts("|\n");
    }

    // Save for pagination
    last_dump_addr = addr;
    last_dump_len = len;
}

void cmd_write(uint32_t addr, uint8_t value) {
    uint8_t *ptr = (uint8_t *)addr;
    *ptr = value;
    uart_puts("Wrote 0x");
    print_hex_byte(value);
    uart_puts(" to 0x");
    print_hex_word(addr);
    uart_puts("\n");
}

void cmd_read(uint32_t addr) {
    uint8_t *ptr = (uint8_t *)addr;
    uart_puts("0x");
    print_hex_word(addr);
    uart_puts(" = 0x");
    print_hex_byte(*ptr);
    uart_puts("\n");
}

void cmd_copy(uint32_t src, uint32_t dst, uint32_t len) {
    uart_puts("Copying ");
    print_dec(len);
    uart_puts(" bytes from 0x");
    print_hex_word(src);
    uart_puts(" to 0x");
    print_hex_word(dst);
    uart_puts("\n");

    // Use memmove for safe overlapping copy
    memmove((void *)dst, (void *)src, len);

    uart_puts("Done.\n");
}

void cmd_fill(uint32_t addr, uint32_t len, uint8_t value) {
    memset((void *)addr, value, len);
    uart_puts("Filled ");
    print_dec(len);
    uart_puts(" bytes at 0x");
    print_hex_word(addr);
    uart_puts(" with 0x");
    print_hex_byte(value);
    uart_puts("\n");
}

//==============================================================================
// Simple Upload Protocol Commands
//==============================================================================

// UART callbacks for simple_upload
static void simple_uart_putc(uint8_t c) {
    while (UART_TX_STATUS & 1);  // Wait while busy
    UART_TX_DATA = c;
}

static uint8_t simple_uart_getc(void) {
    while (!(UART_RX_STATUS & 1));  // Wait until data available
    return UART_RX_DATA & 0xFF;
}

void cmd_simple_upload(uint32_t addr) {
    // Flush UART RX buffer FIRST
    uart_flush_rx();

    // Print initial banner (BEFORE protocol starts)
    uart_puts("\n");
    uart_puts("=== FAST Streaming Upload (NO chunking) ===\n");
    uart_puts("Receiving file to address: 0x");
    print_hex_word(addr);
    uart_puts("\n");
    uart_puts("Max size: ");
    print_dec(ZM_MAX_RECEIVE);
    uart_puts(" bytes\n");
    uart_puts("\n");
    uart_puts("Start fw_upload_fast on your PC now...\n");

    // Local variables for protocol
    uint32_t packet_size = 0;
    uint32_t bytes_received = 0;
    uint32_t expected_crc;
    uint32_t calculated_crc;

    // Initialize CRC32 lookup table
    crc32_init();

    // Step 1: Wait for 'R' (Ready) command
    while (1) {
        uint8_t cmd = simple_uart_getc();
        if (cmd == 'R' || cmd == 'r') {
            break;
        }
        // Check for Ctrl-C cancel
        if (cmd == 0x03) {
            uart_puts("*** Upload CANCELLED ***\n");
            return;
        }
    }

    // Step 2: Send ACK 'A' for Ready
    simple_uart_putc('A');

    // Step 3: Receive 4-byte packet size (little-endian)
    for (int i = 0; i < 4; i++) {
        uint8_t byte = simple_uart_getc();
        packet_size |= ((uint32_t)byte) << (i * 8);
    }

    // Step 4: Send ACK 'B' for size received
    simple_uart_putc('B');

    // Validate size
    if (packet_size == 0 || packet_size > ZM_MAX_RECEIVE) {
        uart_puts("*** Upload FAILED - Invalid size ***\n");
        return;
    }

    // NOTE: NO debug messages during protocol! They interfere with data stream
    // All debug output AFTER protocol completes

    // Step 5: STREAM ALL DATA (no chunking, no ACKs!)
    uint8_t *buffer = (uint8_t *)addr;
    while (bytes_received < packet_size) {
        buffer[bytes_received] = simple_uart_getc();
        bytes_received++;
    }

    // Step 6: Calculate CRC32 of received data
    calculated_crc = calculate_crc32(addr, addr + packet_size - 1);

    // Step 7: Wait for 'C' (CRC command)
    uint8_t crc_cmd = simple_uart_getc();
    if (crc_cmd != 'C') {
        // Protocol failed - NOW we can print debug
        uart_puts("*** Upload FAILED - Expected CRC command, got: 0x");
        print_hex_byte(crc_cmd);
        uart_puts(" ***\n");
        return;
    }

    // Step 8: Receive 4-byte expected CRC (little-endian)
    expected_crc = 0;
    for (int i = 0; i < 4; i++) {
        uint8_t byte = simple_uart_getc();
        expected_crc |= ((uint32_t)byte) << (i * 8);
    }

    // Step 9: Send ACK 'C' + calculated CRC back to host
    simple_uart_putc('C');

    // Send calculated CRC (little-endian)
    simple_uart_putc((calculated_crc >> 0) & 0xFF);
    simple_uart_putc((calculated_crc >> 8) & 0xFF);
    simple_uart_putc((calculated_crc >> 16) & 0xFF);
    simple_uart_putc((calculated_crc >> 24) & 0xFF);

    // Step 10: PROTOCOL COMPLETE - NOW we can display results
    uart_puts("\n");
    if (calculated_crc == expected_crc) {
        uart_puts("*** Upload SUCCESS ***\n");
        uart_puts("Received: ");
        print_dec(bytes_received);
        uart_puts(" bytes\n");
        uart_puts("Address: 0x");
        print_hex_word(addr);
        uart_puts("\n");
        uart_puts("CRC32: 0x");
        print_hex_word(calculated_crc);
        uart_puts("\n");
    } else {
        uart_puts("*** Upload FAILED - CRC Mismatch ***\n");
        uart_puts("Expected: 0x");
        print_hex_word(expected_crc);
        uart_puts("\n");
        uart_puts("Calculated: 0x");
        print_hex_word(calculated_crc);
        uart_puts("\n");
    }
}

//==============================================================================
// CRC32 Helper Functions (matches simple_upload.c polynomial)
//==============================================================================

static uint32_t crc32_table[256];
static int crc32_initialized = 0;

static void crc32_init(void) {
    if (crc32_initialized) return;
    for (int i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320 : 0);
        }
        crc32_table[i] = crc;
    }
    crc32_initialized = 1;
}

// Calculate CRC32 of a memory block
static uint32_t calculate_crc32(uint32_t start_addr, uint32_t end_addr) {
    crc32_init();
    uint32_t crc = 0xFFFFFFFF;
    for (uint32_t addr = start_addr; addr <= end_addr; addr++) {
        uint8_t byte = *((uint8_t *)addr);
        crc = (crc >> 8) ^ crc32_table[(crc ^ byte) & 0xFF];
    }
    return ~crc;
}

//==============================================================================
// Visual Hex Editor (incurses-based)
//==============================================================================

// Helper: Redraw a single memory unit at given address with optional highlighting
static void redraw_unit(uint32_t addr, uint32_t top_addr, int view_mode, int highlight) {
    // Check if address is visible on screen
    if (addr < top_addr || addr >= top_addr + (21 * 16)) {
        return;  // Not on screen
    }

    uint32_t offset = addr - top_addr;
    int row = offset / 16;
    int bytes_per_unit = (view_mode == 0) ? 1 : (view_mode == 1) ? 2 : 4;
    int max_cursor_x = (view_mode == 0) ? 15 : (view_mode == 1) ? 7 : 3;
    int hex_spacing = (view_mode == 0) ? 3 : (view_mode == 1) ? 5 : 9;
    int col = (offset % 16) / bytes_per_unit;

    if (col > max_cursor_x) return;  // Not aligned for this view mode

    // Draw hex value
    move(row + 2, 10 + (col * hex_spacing));
    if (highlight) attron(A_REVERSE);

    if (view_mode == 0) {
        uint8_t value = ((uint8_t *)addr)[0];
        char hex_str[4];
        snprintf(hex_str, sizeof(hex_str), "%02X ", value);
        addstr(hex_str);
    } else if (view_mode == 1) {
        uint16_t value = ((uint16_t *)addr)[0];
        char hex_str[6];
        snprintf(hex_str, sizeof(hex_str), "%04X ", (unsigned int)value);
        addstr(hex_str);
    } else {
        uint32_t value = ((uint32_t *)addr)[0];
        char hex_str[10];
        snprintf(hex_str, sizeof(hex_str), "%08X ", (unsigned int)value);
        addstr(hex_str);
    }

    if (highlight) standend();

    // Draw ASCII
    int hex_width = (max_cursor_x + 1) * hex_spacing;
    if (highlight) attron(A_REVERSE);
    for (int i = 0; i < bytes_per_unit; i++) {
        uint8_t byte = ((uint8_t *)(addr + i))[0];
        char c = (byte >= 32 && byte < 127) ? byte : '.';
        move(row + 2, 10 + hex_width + 1 + (col * bytes_per_unit) + i);
        addch(c);
    }
    if (highlight) standend();
}

// Helper: Redraw a single row (16 bytes) at given address
static void redraw_row(uint32_t row_addr, uint32_t top_addr, int view_mode,
                       uint32_t mark_start, uint32_t mark_end, int marking) {
    // Check if row is visible on screen
    if (row_addr < top_addr || row_addr >= top_addr + (21 * 16)) {
        return;  // Not on screen
    }

    int bytes_per_unit = (view_mode == 0) ? 1 : (view_mode == 1) ? 2 : 4;
    int max_cursor_x = (view_mode == 0) ? 15 : (view_mode == 1) ? 7 : 3;
    int hex_spacing = (view_mode == 0) ? 3 : (view_mode == 1) ? 5 : 9;
    int row = (row_addr - top_addr) / 16;

    // Draw address
    move(row + 2, 0);
    char addr_str[11];
    snprintf(addr_str, sizeof(addr_str), "%08X: ", (unsigned int)row_addr);
    addstr(addr_str);

    // Draw hex values
    for (int col = 0; col <= max_cursor_x; col++) {
        uint32_t addr = row_addr + (col * bytes_per_unit);
        int highlight = (marking && addr >= mark_start && addr <= mark_end);

        move(row + 2, 10 + (col * hex_spacing));
        if (highlight) attron(A_REVERSE);

        if (view_mode == 0) {
            uint8_t value = ((uint8_t *)addr)[0];
            char hex_str[4];
            snprintf(hex_str, sizeof(hex_str), "%02X ", value);
            addstr(hex_str);
        } else if (view_mode == 1) {
            uint16_t value = ((uint16_t *)addr)[0];
            char hex_str[6];
            snprintf(hex_str, sizeof(hex_str), "%04X ", (unsigned int)value);
            addstr(hex_str);
        } else {
            uint32_t value = ((uint32_t *)addr)[0];
            char hex_str[10];
            snprintf(hex_str, sizeof(hex_str), "%08X ", (unsigned int)value);
            addstr(hex_str);
        }

        if (highlight) standend();
    }

    // Draw ASCII
    addstr(" ");
    for (int i = 0; i < 16; i++) {
        uint32_t addr = row_addr + i;
        int highlight = (marking && addr >= mark_start && addr <= mark_end);

        if (highlight) attron(A_REVERSE);
        uint8_t byte = ((uint8_t *)addr)[0];
        char c = (byte >= 32 && byte < 127) ? byte : '.';
        addch(c);
        if (highlight) standend();
    }
}

// Visual hex editor with curses interface
void cmd_visual(uint32_t start_addr) {
    int cursor_x = 0;   // 0-15 (byte column) or 0-7 (word) or 0-3 (dword)
    int cursor_y = 0;   // 0-20 (row on screen)
    uint32_t top_addr = start_addr & ~0xF;  // Align to 16-byte boundary
    int editing = 0;    // Edit mode flag
    int edit_nibble = 0; // Current nibble being edited
    uint32_t edit_value = 0;  // Value being edited (byte/word/dword)
    int old_cursor_x = -1;  // Track old position for redraw
    int old_cursor_y = -1;
    int need_full_redraw = 1;  // Full redraw on first iteration
    int view_mode = 0;  // 0=byte, 1=word(16-bit), 2=dword(32-bit)
    int max_cursor_x = 15;  // Maximum X position (15 for byte, 7 for word, 3 for dword)
    int bytes_per_unit = 1;  // Bytes per unit (1 for byte, 2 for word, 4 for dword)

    // Search state
    int searching = 0;  // Search input mode flag
    char search_buf[32];  // Search input buffer
    int search_len = 0;  // Current search input length
    uint32_t search_pattern[8];  // Parsed search pattern (up to 8 values)
    int search_pattern_len = 0;  // Number of values in pattern

    // Goto state
    int goto_mode = 0;  // Goto input mode flag
    char goto_buf[16];  // Goto address input buffer
    int goto_len = 0;   // Current goto input length

    // Mark state for block operations
    int marking = 0;         // 0=no marks, 1=start marked, 2=both marked
    uint32_t mark_start = 0; // Start address of marked block
    uint32_t mark_end = 0;   // End address of marked block
    int old_marking = 0;     // Previous marking state for incremental updates
    uint32_t old_mark_start = 0;  // Previous mark start for incremental updates
    uint32_t old_mark_end = 0;    // Previous mark end for incremental updates

    // Initialize curses
    initscr();
    noecho();
    raw();
    keypad(stdscr, TRUE);

    while (1) {
        // Only do full redraw if needed (first time or page change)
        if (need_full_redraw) {
            clear();

            // Draw title bar with view mode
            move(0, 0);
            attron(A_REVERSE);
            const char *mode_str = (view_mode == 0) ? "BYTE" : (view_mode == 1) ? "WORD" : "DWORD";
            char title[81];
            snprintf(title, sizeof(title), "Hex Editor [%s] - Arrows:nav Shift+Arrows:select Enter:edit W:mode G:goto M:mark Q:exit", mode_str);
            addstr(title);
            for (int i = strlen(title); i < COLS; i++) addch(' ');
            standend();

            // Draw hex grid (21 rows of 16 bytes each)
            for (int row = 0; row < 21; row++) {
                uint32_t addr = top_addr + (row * 16);
                move(row + 2, 0);

                // Print address
                char addr_str[12];
                snprintf(addr_str, sizeof(addr_str), "%08X: ", (unsigned int)addr);
                addstr(addr_str);

                // No visual highlighting - only status bar shows selection range

                // Print hex data based on view mode
                if (view_mode == 0) {
                    // Byte view: 16 bytes
                    for (int col = 0; col < 16; col++) {
                        uint8_t byte = ((uint8_t *)(addr))[col];
                        char hex_str[4];
                        snprintf(hex_str, sizeof(hex_str), "%02X ", byte);
                        addstr(hex_str);
                    }
                } else if (view_mode == 1) {
                    // Word view: 8 words (16-bit)
                    for (int col = 0; col < 8; col++) {
                        uint16_t word = ((uint16_t *)(addr))[col];
                        char hex_str[6];
                        snprintf(hex_str, sizeof(hex_str), "%04X ", (unsigned int)word);
                        addstr(hex_str);
                    }
                } else {
                    // Dword view: 4 dwords (32-bit)
                    for (int col = 0; col < 4; col++) {
                        uint32_t dword = ((uint32_t *)(addr))[col];
                        char hex_str[10];
                        snprintf(hex_str, sizeof(hex_str), "%08X ", (unsigned int)dword);
                        addstr(hex_str);
                    }
                }

                // Print ASCII
                addstr(" ");
                for (int col = 0; col < 16; col++) {
                    uint8_t byte = ((uint8_t *)(addr))[col];
                    char c = (byte >= 32 && byte < 127) ? byte : '.';
                    addch(c);
                }
            }

            need_full_redraw = 0;
            old_cursor_x = -1;  // Force highlight draw
            old_cursor_y = -1;
        }

        // Calculate bytes per unit and hex spacing based on view mode
        bytes_per_unit = (view_mode == 0) ? 1 : (view_mode == 1) ? 2 : 4;
        int hex_spacing = (view_mode == 0) ? 3 : (view_mode == 1) ? 5 : 9;

        // Redraw old cursor position (unhighlight or keep highlighted if in selection)
        if (old_cursor_x >= 0 && old_cursor_y >= 0) {
            uint32_t old_addr = top_addr + (old_cursor_y * 16) + (old_cursor_x * bytes_per_unit);

            // If we're marking and old position is still in the selection, keep it highlighted
            int should_highlight = 0;
            if (marking == 1) {
                uint32_t current_addr = top_addr + (cursor_y * 16) + (cursor_x * bytes_per_unit);
                uint32_t range_start = (mark_start < current_addr) ? mark_start : current_addr;
                uint32_t range_end = (mark_start < current_addr) ? current_addr : mark_start;
                if (old_addr >= range_start && old_addr <= range_end) {
                    should_highlight = 1;
                }
            }

            // Use redraw_unit to properly handle highlighting state
            redraw_unit(old_addr, top_addr, view_mode, should_highlight);
        }

        // Draw new cursor position (highlight)
        if (!editing) {
            uint32_t new_addr = top_addr + (cursor_y * 16) + (cursor_x * bytes_per_unit);

            // Highlight hex based on view mode
            move(cursor_y + 2, 10 + (cursor_x * hex_spacing));
            attron(A_REVERSE);
            if (view_mode == 0) {
                uint8_t value = ((uint8_t *)new_addr)[0];
                char hex_str[4];
                snprintf(hex_str, sizeof(hex_str), "%02X ", value);
                addstr(hex_str);
            } else if (view_mode == 1) {
                uint16_t value = ((uint16_t *)new_addr)[0];
                char hex_str[6];
                snprintf(hex_str, sizeof(hex_str), "%04X ", (unsigned int)value);
                addstr(hex_str);
            } else {
                uint32_t value = ((uint32_t *)new_addr)[0];
                char hex_str[10];
                snprintf(hex_str, sizeof(hex_str), "%08X ", (unsigned int)value);
                addstr(hex_str);
            }
            standend();

            // Highlight ASCII (multiple bytes for word/dword)
            // ASCII position: address (10) + hex width + space (1)
            int hex_width = (max_cursor_x + 1) * hex_spacing;
            attron(A_REVERSE);
            for (int i = 0; i < bytes_per_unit; i++) {
                uint8_t byte = ((uint8_t *)(new_addr + i))[0];
                char c = (byte >= 32 && byte < 127) ? byte : '.';
                move(cursor_y + 2, 10 + hex_width + 1 + (cursor_x * bytes_per_unit) + i);
                addch(c);
            }
            standend();
        }

        // Status bar
        move(LINES - 1, 0);
        attron(A_REVERSE);
        char status[COLS + 1];
        uint32_t current_addr = top_addr + (cursor_y * 16) + (cursor_x * bytes_per_unit);

        // Display goto/search input or normal status
        if (goto_mode) {
            // Show goto input prompt
            snprintf(status, sizeof(status), "Goto: %s_", goto_buf);
            addstr(status);
            for (int i = strlen(status); i < COLS; i++) addch(' ');
        } else if (searching) {
            // Show search input prompt
            snprintf(status, sizeof(status), "Search: %s_", search_buf);
            addstr(status);
            for (int i = strlen(status); i < COLS; i++) addch(' ');
        } else if (marking == 2) {
            // Show mark range and CRC32
            uint32_t range_size = mark_end - mark_start + 1;
            uint32_t crc = calculate_crc32(mark_start, mark_end);
            snprintf(status, sizeof(status),
                     "MARK: 0x%08X-0x%08X (%u bytes) CRC32:0x%08X",
                     (unsigned int)mark_start, (unsigned int)mark_end,
                     (unsigned int)range_size, (unsigned int)crc);
            addstr(status);
            for (int i = strlen(status); i < COLS; i++) addch(' ');
        } else if (marking == 1) {
            // Show mark start and live range preview
            uint32_t range_start = (mark_start < current_addr) ? mark_start : current_addr;
            uint32_t range_end = (mark_start < current_addr) ? current_addr : mark_start;
            uint32_t range_size = range_end - range_start + 1;
            snprintf(status, sizeof(status),
                     "MARK: 0x%08X-0x%08X (%u bytes) - press M to confirm",
                     (unsigned int)range_start, (unsigned int)range_end,
                     (unsigned int)range_size);
            addstr(status);
            for (int i = strlen(status); i < COLS; i++) addch(' ');
        } else {
            // Display value based on view mode
            if (view_mode == 0) {
                uint8_t value = ((uint8_t *)current_addr)[0];
                snprintf(status, sizeof(status),
                         "Addr:0x%08X Val:0x%02X %s",
                         (unsigned int)current_addr, value,
                         editing ? "EDIT" : "");
            } else if (view_mode == 1) {
                uint16_t value = ((uint16_t *)current_addr)[0];
                snprintf(status, sizeof(status),
                         "Addr:0x%08X Val:0x%04X %s",
                         (unsigned int)current_addr, (unsigned int)value,
                         editing ? "EDIT" : "");
            } else {
                uint32_t value = ((uint32_t *)current_addr)[0];
                snprintf(status, sizeof(status),
                         "Addr:0x%08X Val:0x%08X %s",
                         (unsigned int)current_addr, (unsigned int)value,
                         editing ? "EDIT" : "");
            }
            addstr(status);
            for (int i = strlen(status); i < COLS; i++) addch(' ');
        }
        standend();

        // Incremental highlight updates for shift+arrow selection (marking==1 only)
        if (marking == 1) {
            // Calculate current marked range
            uint32_t range_start = (mark_start < current_addr) ? mark_start : current_addr;
            uint32_t range_end = (mark_start < current_addr) ? current_addr : mark_start;

            if (old_marking == 1) {
                // We were marking before - do incremental update
                // Unhighlight cells that left the range
                uint32_t old_start = (old_mark_start < old_mark_end) ? old_mark_start : old_mark_end;
                uint32_t old_end = (old_mark_start < old_mark_end) ? old_mark_end : old_mark_start;

                // Unhighlight region that's no longer selected
                for (uint32_t addr = old_start; addr <= old_end; addr += bytes_per_unit) {
                    if (addr < range_start || addr > range_end) {
                        redraw_unit(addr, top_addr, view_mode, 0);
                    }
                }

                // Highlight region that's newly selected
                for (uint32_t addr = range_start; addr <= range_end; addr += bytes_per_unit) {
                    if (addr < old_start || addr > old_end) {
                        redraw_unit(addr, top_addr, view_mode, 1);
                    }
                }
            } else {
                // First time marking - highlight entire range
                for (uint32_t addr = range_start; addr <= range_end; addr += bytes_per_unit) {
                    redraw_unit(addr, top_addr, view_mode, 1);
                }
            }

            // Update old range
            old_marking = 1;
            old_mark_start = mark_start;
            old_mark_end = current_addr;
        } else if (old_marking == 1) {
            // We were marking but stopped - unhighlight everything
            uint32_t old_start = (old_mark_start < old_mark_end) ? old_mark_start : old_mark_end;
            uint32_t old_end = (old_mark_start < old_mark_end) ? old_mark_end : old_mark_start;
            for (uint32_t addr = old_start; addr <= old_end; addr += bytes_per_unit) {
                redraw_unit(addr, top_addr, view_mode, 0);
            }
            old_marking = 0;
            old_mark_start = 0;
            old_mark_end = 0;
        }

        // Cursor management
        if (goto_mode) {
            // Show cursor at goto input position
            curs_set(1);
            move(LINES - 1, 6 + goto_len);  // Position after "Goto: " prompt
        } else if (searching) {
            // Show cursor at search input position
            curs_set(1);
            move(LINES - 1, 8 + search_len);  // Position after "Search: " prompt
        } else if (editing) {
            // Show cursor and position it at the edit location
            curs_set(1);
            move(cursor_y + 2, 10 + (cursor_x * hex_spacing) + edit_nibble);
        } else {
            // Hide cursor when navigating
            curs_set(0);
        }

        refresh();

        // Get key - handle escape sequences for arrow keys
        int ch = getch();

        // Handle escape sequences (arrow keys send ESC [ A/B/C/D)
        // Shift+arrow keys send ESC [ 1 ; 2 A/B/C/D
        if (ch == 27) {  // ESC
            int ch2 = getch();
            if (ch2 == '[') {
                int ch3 = getch();
                if (ch3 == '1') {
                    // Could be shift+arrow: ESC [ 1 ; 2 A/B/C/D
                    int ch4 = getch();
                    if (ch4 == ';') {
                        int ch5 = getch();
                        if (ch5 == '2') {
                            int ch6 = getch();
                            // Shift+arrow keys
                            switch (ch6) {
                                case 'A': ch = 165; break;  // Shift+Up
                                case 'B': ch = 166; break;  // Shift+Down
                                case 'C': ch = 167; break;  // Shift+Right
                                case 'D': ch = 168; break;  // Shift+Left
                                default: ch = 27; break;
                            }
                        } else {
                            ch = 27;  // Unknown sequence
                        }
                    } else {
                        ch = 27;  // Unknown sequence
                    }
                } else {
                    // Regular arrow keys: ESC [ A/B/C/D
                    switch (ch3) {
                        case 'A': ch = 65; break;  // Up arrow
                        case 'B': ch = 66; break;  // Down arrow
                        case 'C': ch = 67; break;  // Right arrow
                        case 'D': ch = 68; break;  // Left arrow
                        default: ch = 27; break;   // Unknown, treat as ESC
                    }
                }
            }
            // If not '[', fall through with ESC
        }

        if (editing) {
            // Determine max nibbles based on view mode
            int max_nibbles = (view_mode == 0) ? 2 : (view_mode == 1) ? 4 : 8;

            // Edit mode - accept hex digits
            int digit = -1;
            if (ch >= '0' && ch <= '9') {
                digit = ch - '0';
            } else if ((ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F')) {
                digit = ((ch & 0xDF) - 'A') + 10;
            } else if (ch == 27) {  // ESC - cancel edit
                editing = 0;
                edit_nibble = 0;
                digit = -1;
            }

            if (digit >= 0) {
                // Add this nibble to edit_value
                if (edit_nibble == 0) {
                    edit_value = 0;  // Reset for new value
                }
                edit_value = (edit_value << 4) | digit;
                edit_nibble++;

                // Check if we've entered all nibbles
                if (edit_nibble >= max_nibbles) {
                    // Write the value based on view mode
                    uint32_t addr = top_addr + (cursor_y * 16) + (cursor_x * bytes_per_unit);
                    if (view_mode == 0) {
                        *((uint8_t *)addr) = (uint8_t)edit_value;
                    } else if (view_mode == 1) {
                        *((uint16_t *)addr) = (uint16_t)edit_value;
                    } else {
                        *((uint32_t *)addr) = edit_value;
                    }

                    // Save position for redraw
                    old_cursor_x = cursor_x;
                    old_cursor_y = cursor_y;

                    editing = 0;
                    edit_nibble = 0;

                    // Move to next unit
                    cursor_x++;
                    if (cursor_x > max_cursor_x) {
                        cursor_x = 0;
                        cursor_y++;
                        if (cursor_y >= 21) {
                            cursor_y = 20;
                            top_addr += 16;
                            need_full_redraw = 1;
                        }
                    }
                }
            }
        } else if (goto_mode) {
            // Goto input mode - accept hex digits for address
            if (ch == '\n' || ch == '\r') {
                // Enter pressed - parse and execute goto
                goto_mode = 0;

                // Parse goto buffer as hex address
                uint32_t goto_addr = 0;
                char *p = goto_buf;
                while (*p) {
                    int digit = -1;
                    if (*p >= '0' && *p <= '9') {
                        digit = *p - '0';
                    } else if ((*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F')) {
                        digit = ((*p & 0xDF) - 'A') + 10;
                    }
                    if (digit >= 0) {
                        goto_addr = (goto_addr << 4) | digit;
                    }
                    p++;
                }

                // Center the display on the goto address
                uint32_t goto_row = (goto_addr & ~0xF);  // Align to 16-byte boundary
                // Try to center vertically (10 rows above puts result in middle)
                if (goto_row >= (10 * 16)) {
                    top_addr = goto_row - (10 * 16);
                } else {
                    top_addr = 0;
                }

                // Position cursor on the goto location
                cursor_y = ((goto_addr - top_addr) / 16);
                cursor_x = ((goto_addr - top_addr - (cursor_y * 16)) / bytes_per_unit);

                need_full_redraw = 1;
                old_cursor_x = -1;
                old_cursor_y = -1;
            } else if (ch == 27) {  // ESC - cancel goto
                goto_mode = 0;
                goto_len = 0;
                goto_buf[0] = '\0';
            } else if (ch == 8 || ch == 127) {  // Backspace
                if (goto_len > 0) {
                    goto_len--;
                    goto_buf[goto_len] = '\0';
                }
            } else if ((ch >= '0' && ch <= '9') ||
                       (ch >= 'a' && ch <= 'f') ||
                       (ch >= 'A' && ch <= 'F')) {
                // Add hex digit to goto buffer
                if (goto_len < (int)(sizeof(goto_buf) - 1)) {
                    goto_buf[goto_len++] = ch;
                    goto_buf[goto_len] = '\0';
                }
            }
        } else if (searching) {
            // Search input mode - accept hex digits and spaces
            if (ch == '\n' || ch == '\r') {
                // Enter pressed - parse and execute search
                searching = 0;

                // Parse search buffer into pattern based on view mode
                search_pattern_len = 0;
                char *p = search_buf;
                while (*p && search_pattern_len < 8) {
                    // Skip spaces
                    while (*p == ' ') p++;
                    if (!*p) break;

                    // Parse hex value
                    uint32_t value = 0;
                    int nibbles = 0;
                    int max_nibbles = (view_mode == 0) ? 2 : (view_mode == 1) ? 4 : 8;

                    while (*p && *p != ' ' && nibbles < max_nibbles) {
                        int digit = -1;
                        if (*p >= '0' && *p <= '9') {
                            digit = *p - '0';
                        } else if ((*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F')) {
                            digit = ((*p & 0xDF) - 'A') + 10;
                        }
                        if (digit >= 0) {
                            value = (value << 4) | digit;
                            nibbles++;
                        }
                        p++;
                    }

                    if (nibbles > 0) {
                        search_pattern[search_pattern_len++] = value;
                    }
                }

                // Perform search from current position
                if (search_pattern_len > 0) {
                    uint32_t search_start = top_addr + (cursor_y * 16) + (cursor_x * bytes_per_unit) + bytes_per_unit;
                    uint32_t search_end = 0x00080000;  // End of SRAM

                    for (uint32_t addr = search_start; addr < search_end; addr += bytes_per_unit) {
                        // Check if pattern matches at this address
                        int match = 1;
                        for (int i = 0; i < search_pattern_len; i++) {
                            uint32_t check_addr = addr + (i * bytes_per_unit);
                            uint32_t mem_value = 0;

                            if (view_mode == 0) {
                                mem_value = ((uint8_t *)check_addr)[0];
                            } else if (view_mode == 1) {
                                mem_value = ((uint16_t *)check_addr)[0];
                            } else {
                                mem_value = ((uint32_t *)check_addr)[0];
                            }

                            if (mem_value != search_pattern[i]) {
                                match = 0;
                                break;
                            }
                        }

                        if (match) {
                            // Center the display on the found address
                            uint32_t found_row = (addr & ~0xF);  // Align to 16-byte boundary
                            // Try to center vertically (10 rows above puts result in middle)
                            if (found_row >= (10 * 16)) {
                                top_addr = found_row - (10 * 16);
                            } else {
                                top_addr = 0;
                            }

                            // Position cursor on the found location
                            cursor_y = ((addr - top_addr) / 16);
                            cursor_x = ((addr - top_addr - (cursor_y * 16)) / bytes_per_unit);

                            need_full_redraw = 1;
                            old_cursor_x = -1;
                            old_cursor_y = -1;
                            break;
                        }
                    }
                }
            } else if (ch == 27) {  // ESC - cancel search
                searching = 0;
                search_len = 0;
                search_buf[0] = '\0';
            } else if (ch == 8 || ch == 127) {  // Backspace
                if (search_len > 0) {
                    search_len--;
                    search_buf[search_len] = '\0';
                }
            } else if ((ch >= '0' && ch <= '9') ||
                       (ch >= 'a' && ch <= 'f') ||
                       (ch >= 'A' && ch <= 'F') ||
                       ch == ' ') {
                // Add character to search buffer
                if (search_len < (int)(sizeof(search_buf) - 1)) {
                    search_buf[search_len++] = ch;
                    search_buf[search_len] = '\0';
                }
            }
        } else {
            // Navigation mode

            // Clear marks on any key press when marking==2 (confirmed selection)
            // Exception: 'm' key to start new selection is handled in its own case
            if (marking == 2 && ch != 'm' && ch != 'M') {
                marking = 0;
                mark_start = 0;
                mark_end = 0;
            }

            switch (ch) {
                case 27:   // ESC - exit visual mode
                case 'q':
                case 'Q':
                    endwin();
                    return;

                case '\n':  // Enter - start editing
                case '\r':
                    editing = 1;
                    edit_nibble = 0;
                    edit_value = 0;
                    break;

                // Arrow keys (curses KEY_* constants)
                case 'h':  // Left (vi-style)
                case 68:   // Left arrow (if keypad works)
                    if (cursor_x > 0) {
                        old_cursor_x = cursor_x;
                        old_cursor_y = cursor_y;
                        cursor_x--;
                    }
                    break;

                case 'l':  // Right (vi-style)
                case 67:   // Right arrow
                    if (cursor_x < max_cursor_x) {
                        old_cursor_x = cursor_x;
                        old_cursor_y = cursor_y;
                        cursor_x++;
                    }
                    break;

                case 'k':  // Up (vi-style)
                case 65:   // Up arrow
                    if (cursor_y > 0) {
                        old_cursor_x = cursor_x;
                        old_cursor_y = cursor_y;
                        cursor_y--;
                    } else if (top_addr >= 16) {
                        top_addr -= 16;
                        need_full_redraw = 1;
                    }
                    break;

                case 'j':  // Down (vi-style)
                case 66:   // Down arrow
                    if (cursor_y < 20) {
                        old_cursor_x = cursor_x;
                        old_cursor_y = cursor_y;
                        cursor_y++;
                    } else {
                        top_addr += 16;
                        need_full_redraw = 1;
                    }
                    break;

                case ' ':  // Page down
                case 'f':  // Page forward
                    top_addr += (21 * 16);
                    need_full_redraw = 1;
                    break;

                case 'b':  // Page back
                    if (top_addr >= (21 * 16)) {
                        top_addr -= (21 * 16);
                    } else {
                        top_addr = 0;
                    }
                    need_full_redraw = 1;
                    break;

                case 'g':  // Go to address with input
                case 'G':
                    goto_mode = 1;
                    goto_len = 0;
                    goto_buf[0] = '\0';
                    break;

                case 'w':  // Cycle view mode (byte -> word -> dword)
                case 'W':
                    view_mode = (view_mode + 1) % 3;
                    // Update max cursor position based on view mode
                    if (view_mode == 0) {
                        max_cursor_x = 15;  // 16 bytes
                    } else if (view_mode == 1) {
                        max_cursor_x = 7;   // 8 words
                    } else {
                        max_cursor_x = 3;   // 4 dwords
                    }
                    // Adjust cursor if out of bounds
                    if (cursor_x > max_cursor_x) {
                        cursor_x = max_cursor_x;
                    }
                    need_full_redraw = 1;
                    break;

                case '/':  // Start search
                    searching = 1;
                    search_len = 0;
                    search_buf[0] = '\0';
                    break;

                // Shift+arrow keys for text-editor-style selection
                case 168:  // Shift+Left
                case 167:  // Shift+Right
                case 165:  // Shift+Up
                case 166:  // Shift+Down
                    // Start selection if not already marking
                    if (marking == 0) {
                        mark_start = current_addr;
                        marking = 1;
                    } else if (marking == 2) {
                        // Clear marks and exit marking mode
                        marking = 0;
                        mark_start = 0;
                        mark_end = 0;
                        break;  // Don't process the arrow key, just clear
                    }

                    // Move cursor based on direction
                    if (ch == 168 && cursor_x > 0) {  // Shift+Left
                        old_cursor_x = cursor_x;
                        old_cursor_y = cursor_y;
                        cursor_x--;
                    } else if (ch == 167 && cursor_x < max_cursor_x) {  // Shift+Right
                        old_cursor_x = cursor_x;
                        old_cursor_y = cursor_y;
                        cursor_x++;
                    } else if (ch == 165) {  // Shift+Up
                        if (cursor_y > 0) {
                            old_cursor_x = cursor_x;
                            old_cursor_y = cursor_y;
                            cursor_y--;
                        } else if (top_addr >= 16) {
                            // Scroll up one row - redraw incrementally
                            top_addr -= 16;
                            // Scroll screen content down by inserting line at top
                            move(2, 0);
                            insertln();
                            // Redraw the new top row with current selection state
                            uint32_t current_addr = top_addr + (cursor_y * 16) + (cursor_x * bytes_per_unit);
                            uint32_t range_start = (mark_start < current_addr) ? mark_start : current_addr;
                            uint32_t range_end = (mark_start < current_addr) ? current_addr : mark_start;
                            redraw_row(top_addr, top_addr, view_mode, range_start, range_end, marking);
                        }
                    } else if (ch == 166) {  // Shift+Down
                        if (cursor_y < 20) {
                            old_cursor_x = cursor_x;
                            old_cursor_y = cursor_y;
                            cursor_y++;
                        } else {
                            // Scroll down one row - redraw incrementally
                            top_addr += 16;
                            // Scroll screen content up by deleting top line
                            move(2, 0);
                            deleteln();
                            // Redraw the new bottom row with current selection state
                            uint32_t current_addr = top_addr + (cursor_y * 16) + (cursor_x * bytes_per_unit);
                            uint32_t range_start = (mark_start < current_addr) ? mark_start : current_addr;
                            uint32_t range_end = (mark_start < current_addr) ? current_addr : mark_start;
                            uint32_t bottom_row_addr = top_addr + (20 * 16);
                            move(22, 0);  // Move to bottom row position
                            redraw_row(bottom_row_addr, top_addr, view_mode, range_start, range_end, marking);
                        }
                    }
                    break;

                case 'm':  // Mark/unmark for block operations
                case 'M':
                    if (marking == 0) {
                        // First press: set mark start
                        mark_start = current_addr;
                        marking = 1;
                    } else if (marking == 1) {
                        // Second press: set mark end (confirm)
                        mark_end = current_addr;
                        // Ensure start < end
                        if (mark_start > mark_end) {
                            uint32_t temp = mark_start;
                            mark_start = mark_end;
                            mark_end = temp;
                        }
                        marking = 2;
                    } else {
                        // Third press: unhighlight old selection and start new mark
                        // Unhighlight old confirmed selection incrementally
                        for (uint32_t addr = mark_start; addr <= mark_end; addr += bytes_per_unit) {
                            redraw_unit(addr, top_addr, view_mode, 0);
                        }
                        // Start new mark
                        mark_start = current_addr;
                        marking = 1;
                    }
                    break;
            }
        }
    }
}

//==============================================================================
// MicroRL Callbacks
//==============================================================================

// Output callback for microRL - print string to UART
int microrl_output(microrl_t *mrl, const char *str) {
    (void)mrl;  // Unused
    uart_puts(str);
    return 0;
}

// Execute callback for microRL - rebuild command line and execute
int microrl_execute(microrl_t *mrl, int argc, const char* const *argv) {
    (void)mrl;  // Unused

    if (argc == 0) {
        return 0;  // Empty command
    }

    // Rebuild command line from argc/argv
    char cmdline[128];
    int pos = 0;

    for (int i = 0; i < argc && pos < 127; i++) {
        if (i > 0) {
            cmdline[pos++] = ' ';  // Space between arguments
        }
        const char *arg = argv[i];
        while (*arg && pos < 127) {
            cmdline[pos++] = *arg++;
        }
    }
    cmdline[pos] = '\0';

    // Execute using existing parser
    execute_command(cmdline);

    return 0;
}

//==============================================================================
// Command Parser Utilities
//==============================================================================

// Parse hex number from string
uint32_t parse_hex(const char *str, const char **end) {
    uint32_t val = 0;

    // Skip "0x" prefix if present
    if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
        str += 2;
    }

    while (*str) {
        int digit = hex_to_val(*str);
        if (digit < 0) break;
        val = (val << 4) | digit;
        str++;
    }

    if (end) *end = str;
    return val;
}

void skip_whitespace(const char **str) {
    while (**str == ' ' || **str == '\t') {
        (*str)++;
    }
}

void execute_command(const char *cmd) {
    skip_whitespace(&cmd);

    if (*cmd == '\0') {
        return;  // Empty command
    }

    char op = *cmd++;
    skip_whitespace(&cmd);

    switch (op) {
        case 'd':  // Dump memory
        case 'D': {
            uint32_t addr = parse_hex(cmd, &cmd);
            skip_whitespace(&cmd);
            uint32_t len = parse_hex(cmd, &cmd);
            if (len == 0) len = 256;  // Default 256 bytes
            cmd_dump(addr, len);
            break;
        }

        case 'r':  // Read byte
        case 'R': {
            uint32_t addr = parse_hex(cmd, &cmd);
            cmd_read(addr);
            break;
        }

        case 'w':  // Write byte
        case 'W': {
            uint32_t addr = parse_hex(cmd, &cmd);
            skip_whitespace(&cmd);
            uint8_t value = (uint8_t)parse_hex(cmd, &cmd);
            cmd_write(addr, value);
            break;
        }

        case 'c':  // Copy memory
        case 'C': {
            uint32_t src = parse_hex(cmd, &cmd);
            skip_whitespace(&cmd);
            uint32_t dst = parse_hex(cmd, &cmd);
            skip_whitespace(&cmd);
            uint32_t len = parse_hex(cmd, &cmd);
            if (len > 0) {
                cmd_copy(src, dst, len);
            } else {
                uart_puts("Usage: c <src> <dst> <len>\n");
            }
            break;
        }

        case 'f':  // Fill memory
        case 'F': {
            uint32_t addr = parse_hex(cmd, &cmd);
            skip_whitespace(&cmd);
            uint32_t len = parse_hex(cmd, &cmd);
            skip_whitespace(&cmd);
            uint8_t value = (uint8_t)parse_hex(cmd, &cmd);
            if (len > 0) {
                cmd_fill(addr, len, value);
            } else {
                uart_puts("Usage: f <addr> <len> <value>\n");
            }
            break;
        }

        case 'u':  // Upload using bootloader protocol
        case 'U': {
            // Check if this is 'up' (upload from PC)
            if (*cmd == 'p' || *cmd == 'P') {
                cmd++;  // Skip 'p'/'P'
                skip_whitespace(&cmd);
                uint32_t addr = parse_hex(cmd, &cmd);
                if (addr == 0 && *cmd == '\0') {
                    addr = ZM_BUFFER_ADDR;  // Default to transfer buffer
                }
                cmd_simple_upload(addr);
            } else {
                uart_puts("Upload command:\n");
                uart_puts("  up [addr]  - Upload file (bootloader protocol)\n");
                uart_puts("               Default addr: 0x");
                print_hex_word(ZM_BUFFER_ADDR);
                uart_puts("\n");
            }
            break;
        }

        case 't':  // Toggle clock display
        case 'T': {
            clock_enabled = !clock_enabled;
            if (clock_enabled) {
                uart_puts("Clock display enabled\n");
            } else {
                uart_puts("Clock display disabled\n");
                // Clear the clock area
                uart_puts("\033[s");         // Save cursor
                uart_puts("\033[1;60H");     // Move to clock position
                uart_puts("               ");  // Clear with spaces
                uart_puts("\033[u");         // Restore cursor
            }
            break;
        }

        case 'v':  // Visual hex editor
        case 'V': {
            uint32_t addr = 0;
            if (*cmd != '\0') {
                addr = parse_hex(cmd, &cmd);
            }
            cmd_visual(addr);
            // After exiting visual mode, clear screen and show prompt
            uart_puts("\033[2J\033[H");  // Clear screen, home cursor
            uart_puts("Exited visual mode\n");
            break;
        }

        case 'h':  // Help
        case 'H':
        case '?': {
            uart_puts("\n");
            uart_puts("Commands:\n");
            uart_puts("  d <addr> [len]           - Dump memory (hex+ASCII)\n");
            uart_puts("  SPACE                    - Page to next 256 bytes\n");
            uart_puts("  r <addr>                 - Read byte\n");
            uart_puts("  w <addr> <value>         - Write byte\n");
            uart_puts("  c <src> <dst> <len>      - Copy memory block\n");
            uart_puts("  f <addr> <len> <val>     - Fill memory\n");
            uart_puts("  v [addr]                 - Visual hex editor (curses)\n");
            uart_puts("  t                        - Toggle clock display on/off\n");
            uart_puts("  up [addr]                - Upload file (bootloader protocol)\n");
            uart_puts("  h or ?                   - This help\n");
            uart_puts("\n");
            uart_puts("Addresses and values in hex (0x optional)\n");
            uart_puts("Default dump: 256 bytes (0x100)\n");
            uart_puts("Transfer buffer at: 0x");
            print_hex_word(ZM_BUFFER_ADDR);
            uart_puts(" (128KB max)\n");
            uart_puts("\n");
            break;
        }

        default:
            uart_puts("Unknown command. Type 'h' for help.\n");
            break;
    }
}

//==============================================================================
// Clock Display
//==============================================================================

void print_clock(void) {
    // Save cursor position
    uart_puts("\033[s");

    // Move to top-right (row 1, col 60)
    uart_puts("\033[1;60H");

    // Print clock: HH:MM:SS:FF
    char buf[16];
    snprintf(buf, sizeof(buf), "[%02u:%02u:%02u:%02u]",
             (unsigned int)clock_hours,
             (unsigned int)clock_minutes,
             (unsigned int)clock_seconds,
             (unsigned int)clock_frames);
    uart_puts(buf);

    // Restore cursor position
    uart_puts("\033[u");
}

//==============================================================================
// Main
//==============================================================================

int main(void) {
    microrl_t mrl;

    // Initialize hardware timer for 60 Hz interrupts
    timer_init();

    // Enable Timer IRQ (IRQ[0])
    uart_puts("Enabling timer interrupts...\n");
    irq_enable();

    // Initialize microRL
    microrl_init(&mrl, microrl_output, microrl_execute);
    microrl_set_prompt(&mrl, "> ");

    uart_puts("\n");
    uart_puts("===========================================\n");
    uart_puts("  PicoRV32 Hex Editor + microRL\n");
    uart_puts("===========================================\n");
    uart_puts("Type 'h' for help, 't' to toggle clock display\n");
    uart_puts("Features: Command history (UP/DOWN), line editing\n");
    uart_puts("\n");

    while (1) {
        // Update clock display if timer interrupt fired and enabled
        if (clock_updated && clock_enabled) {
            clock_updated = 0;
            print_clock();
        }

        // Check for UART input (non-blocking)
        if (!uart_getc_available()) {
            continue;  // No input yet, keep checking clock
        }

        char c = uart_getc();

        // Spacebar: page to next 256 bytes (special handling before microRL)
        if (c == ' ' && mrl.cmdlen == 0) {
            // Only handle spacebar if command line is empty
            uart_puts("\n");
            uint32_t next_addr = last_dump_addr + last_dump_len;
            cmd_dump(next_addr, 0x100);
            microrl_set_prompt(&mrl, "> ");  // Reprint prompt
            continue;
        }

        // Feed character to microRL for processing
        microrl_processing_input(&mrl, &c, 1);
    }

    return 0;
}
