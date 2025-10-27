//==============================================================================
// Olimex iCE40HX8K-EVB RISC-V Platform
// hexedit overlay - Interactive Hex Editor for Overlay System
//
// Copyright (c) October 2025 Michael Wolak
// Email: mikewolak@gmail.com, mike@epromfoundry.com
//
// NOT FOR COMMERCIAL USE
// Educational and research purposes only
//==============================================================================

/*
 * Visual Hex Editor (Overlay Version):
 * - Full-screen curses-based interface
 * - Navigate with arrow keys or vi-style (hjkl)
 * - Edit memory directly (byte/word/dword modes)
 * - Mark/select memory regions with CRC32 display
 * - Search for hex/ASCII patterns
 * - Goto address with 'g' command
 * - Press 'q' or ESC to exit and return to main menu
 *
 * Launches directly into visual mode at 0x60000 (overlay start)
 * Memory range: 0x60000-0x80000 (128KB overlay space)
 */

#include "hardware.h"
#include "io.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <curses.h>

// Hardware addresses provided by hardware.h
// Timer macros provided by hardware.h

// No clock/interrupt support in overlay mode

// Overlay memory layout (from memory_config.h)
// Code/Data: 0x60000 - 0x78000 (96KB)
// Stack:     0x78000 - 0x7A000 (8KB)
// Heap:      0x7A000 - 0x80000 (24KB)
// Upload removed - overlays can't upload firmware
#define ZM_MAX_RECEIVE    0  // Upload disabled in overlay
#define ZM_BUFFER_ADDR    ((uint8_t *)0x7A000)  // Overlay heap start

// No interrupt support in overlay mode

// No forward declarations needed for visual-only mode

//==============================================================================
// UART Functions (most provided by io.h)
//==============================================================================

// uart_putc, uart_puts, uart_getc_available, uart_getc provided by io.h

// Flush UART RX buffer (discard all pending data)
void uart_flush_rx(void) {
    while (uart_getc_available()) {
        (void)uart_getc();  // Discard byte
    }
}

// getc_timeout removed - overlays don't have timer support

// No interrupt handler in overlay mode

// No timer functions in overlay mode

// All utility and command functions removed - visual mode only

//==============================================================================
// CRC32 Functions (used by mark command)
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

// Command-line mode removed - visual mode only

//==============================================================================
// Main
//==============================================================================

int main(void) {
    uart_puts("\r\n");
    uart_puts("===========================================\r\n");
    uart_puts("  PicoRV32 Visual Hex Editor\r\n");
    uart_puts("===========================================\r\n");
    uart_puts("Starting visual mode...\r\n");
    uart_puts("\r\n");

    // Launch directly into visual hex editor at overlay start address
    cmd_visual(0x60000);

    // After exiting visual mode, clear screen and return to main menu
    uart_puts("\033[2J\033[H");  // Clear screen, home cursor
    uart_puts("\r\n");
    uart_puts("Hex Editor exited. Returning to main menu...\r\n");
    uart_puts("\r\n");

    return 0;
}
