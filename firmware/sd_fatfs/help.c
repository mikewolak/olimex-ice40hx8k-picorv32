//==============================================================================
// Olimex iCE40HX8K-EVB RISC-V Platform
// SD Card Manager - Help System
//
// Interactive multi-page help with wiring diagrams and usage information
//
// Copyright (c) October 2025 Michael Wolak
// Email: mikewolak@gmail.com, mike@epromfoundry.com
//==============================================================================

#include <stdio.h>
#include "../../lib/incurses/curses.h"
#include "help.h"

//==============================================================================
// Help System - Interactive Multi-Page Display
//==============================================================================

void show_help(void) {
    int page = 0;
    int max_page = 2;  // 3 pages total (0, 1, 2)
    int need_redraw = 1;

    flushinp();
    timeout(-1);

    while (1) {
        if (need_redraw) {
            clear();
            int row = 0;

            // Title (on all pages)
            move(row++, 0);
            attron(A_REVERSE);
            addstr("SD CARD MANAGER - Help");
            for (int i = 22; i < COLS; i++) addch(' ');
            standend();
            row++;

        if (page == 0) {
            // Page 1: 34-Pin GPIO Header Pinout (MAIN PINOUT)
            move(row++, 0);
            addstr("  ╔═════════════════════════════════════════════════════════════════════════════╗");
            move(row++, 0);
            addstr("  ║                OLIMEX iCE40HX8K 34-PIN GPIO HEADER PINOUT                   ║");
            move(row++, 0);
            addstr("  ╚═════════════════════════════════════════════════════════════════════════════╝");
            row++;

            move(row++, 2);
            addstr("Pin Layout (viewed from front of board):");
            row++;

            // Top row (odd pins: 33 down to 01)
            move(row++, 0);
            addstr("  33    31    29    27    25    23    21    19    17    15    13    11    09    07    05    03    01");
            move(row++, 0);
            addstr(" ┌─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┐");
            move(row++, 0);
            addstr(" │ F1  │ H6  │ F3  │ G3  │ E2  │ E3  │ G4  │ D1  │ G5  │ C2  │ C1  │ B1  │ F5  │ B2  │ E4  │ 3V3 │ 5V  │");
            move(row++, 0);
            addstr(" │     │     │     │     │     │     │     │     │     │ CS  │MISO │MOSI │ SCK │TX   │RX   │     │     │");
            move(row++, 0);
            addstr(" ├─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┤");
            move(row++, 0);
            addstr(" │ J4  │ H2  │ T1  │ P4  │ R2  │ N5  │ T2  │ P5  │ R3  │ R5  │ T3  │ L2  │ L1  │ GND │ CLK │ GND │ GND │");
            move(row++, 0);
            addstr(" │     │     │     │     │     │     │     │     │     │     │     │     │     │     │100MH│     │     │");
            move(row++, 0);
            addstr(" └─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┘");
            move(row++, 0);
            addstr("  34    32    30    28    26    24    22    20    18    16    14    12    10    08    06    04    02");
            row++;

            move(row++, 2);
            attron(A_REVERSE);
            addstr("SD CARD SPI SIGNALS:");
            standend();
            move(row++, 4);
            addstr("SCK (Clock) / MOSI (Data In) / MISO (Data Out) / CS (Chip Select)");
            row++;

            move(row++, 2);
            attron(A_REVERSE);
            addstr("POWER & GROUND:");
            standend();
            move(row++, 4);
            addstr("5V (Pin 01) | 3V3 (Pin 03) | GND (Pins 02, 04, 06, 08)");
            row++;

            move(row++, 2);
            attron(A_REVERSE);
            addstr("UART:");
            standend();
            move(row++, 4);
            addstr("TX (Pin 07 = Board->PC) | RX (Pin 05 = PC->Board)");

        } else if (page == 1) {
            // Page 2: SD Card Adapter Wiring
            move(row++, 0);
            attron(A_REVERSE);
            addstr("SD CARD ADAPTER MODULE - 8 PIN (with voltage regulator)");
            standend();
            row++;

            move(row++, 2);
            addstr("Module pinout (viewed from bottom with pins facing you):");
            row++;

            // ASCII art SD card module
            move(row++, 4);
            addstr("   ┌─────────────────────────────────┐");
            move(row++, 4);
            addstr("   │    [SD CARD SOCKET - TOP]      │");
            move(row++, 4);
            addstr("   │  (Metal socket, blue PCB)      │");
            move(row++, 4);
            addstr("   │  Has voltage regulator onboard │");
            move(row++, 4);
            addstr("   └─────────────────────────────────┘");
            move(row++, 4);
            addstr("     │   │  │   │  │   │    │   │");
            move(row++, 4);
            addstr("    GND 5V 3V MOSI CS MISO SCK GND");
            row++;

            move(row++, 2);
            attron(A_REVERSE);
            addstr("MODULE PIN CONNECTIONS:");
            standend();
            move(row++, 4);
            addstr("GND  → Header Pin 02, 04, 06, or 08 (Ground)");
            move(row++, 4);
            addstr("5V   → Header Pin 01 (if using 5V power option)");
            move(row++, 4);
            addstr("3V   → Header Pin 03 (if using 3.3V direct option)");
            move(row++, 4);
            addstr("MOSI → Header Pin 11 (B1 = SPI Data In)");
            move(row++, 4);
            addstr("CS   → Header Pin 15 (C2 = SPI Chip Select)");
            move(row++, 4);
            addstr("MISO → Header Pin 13 (C1 = SPI Data Out)");
            move(row++, 4);
            addstr("SCK  → Header Pin 09 (F5 = SPI Clock)");
            move(row++, 4);
            addstr("GND  → Header Pin 02, 04, 06, or 08 (Ground)");
            row++;

            move(row++, 2);
            attron(A_REVERSE);
            addstr("POWER OPTIONS:");
            standend();
            move(row++, 4);
            addstr("Option A (5V): Connect module pin 2 to header pin 01 (+5V)");
            move(row++, 6);
            addstr("→ Onboard regulator converts to 3.3V for SD card");
            move(row++, 4);
            addstr("Option B (3.3V): Connect module pin 3 to header pin 03 (+3.3V)");
            move(row++, 6);
            addstr("→ Bypasses regulator, direct 3.3V to SD card");
            move(row++, 4);
            addstr("Note: Connect BOTH GND pins (1 and 8) for stability!");
            row++;

            move(row++, 2);
            attron(A_REVERSE);
            addstr("IMPORTANT:");
            standend();
            move(row++, 4);
            addstr("• CS is active LOW (pulled low during communication)");
            move(row++, 4);
            addstr("• This adapter has NO card detect pin - use soft eject");
            move(row++, 4);
            addstr("• Module has voltage regulator - can use 5V or 3.3V");

        } else if (page == 2) {
            // Page 3: Usage Info & Troubleshooting
            move(row++, 0);
            attron(A_REVERSE);
            addstr("KEYBOARD CONTROLS");
            standend();
            move(row++, 4);
            addstr("Arrow Up/Down, j/k  : Navigate menu");
            move(row++, 4);
            addstr("Enter               : Select option");
            move(row++, 4);
            addstr("H                   : Show this help");
            move(row++, 4);
            addstr("Q                   : Quit application");
            move(row++, 4);
            addstr("ESC                 : Cancel/back (in submenus)");
            row++;

            move(row++, 0);
            attron(A_REVERSE);
            addstr("MAIN MENU OPTIONS");
            standend();
            move(row++, 4);
            addstr("1. Detect SD Card        - Initialize & mount filesystem");
            move(row++, 4);
            addstr("2. Card Information      - Display CID/CSD registers");
            move(row++, 4);
            addstr("3. Format Card (FAT32)   - Erase & create new filesystem");
            move(row++, 4);
            addstr("4. File Browser          - Browse files (to be implemented)");
            move(row++, 4);
            addstr("5. Upload Overlay (UART) - Receive binary via serial");
            move(row++, 4);
            addstr("6. Upload Bootloader     - Upload uncompressed bootloader");
            move(row++, 4);
            addstr("7. Upload Bootloader.GZ  - Upload gzip-compressed bootloader");
            move(row++, 4);
            addstr("8. Browse & Run Overlays - Load and execute from SD");
            move(row++, 4);
            addstr("9. Upload & Execute (RAM)- Direct upload without SD save");
            row++;

            move(row++, 0);
            attron(A_REVERSE);
            addstr("TECHNICAL INFO");
            standend();
            move(row++, 4);
            addstr("Filesystem: FAT12/16/32/exFAT, long filenames, 512-byte sectors");
            move(row++, 4);
            addstr("SPI Speed: 390 kHz (init) to 50 MHz (high-speed)");
            move(row++, 4);
            addstr("Default: 12.5 MHz (reliable for most cards)");
            row++;

            move(row++, 0);
            attron(A_REVERSE);
            addstr("TROUBLESHOOTING");
            standend();
            move(row++, 4);
            addstr("Card not detected: Check wiring, try slower SPI speed");
            move(row++, 4);
            addstr("Mount failed: Format card on PC first (FAT32)");
            move(row++, 4);
            addstr("Errors during transfer: Reduce SPI speed in menu option 9");
        }

            // Page indicator and navigation help
            move(LINES - 2, 0);
            char page_info[80];
            snprintf(page_info, sizeof(page_info), "Page %d/%d", page + 1, max_page + 1);
            addstr(page_info);

            move(LINES - 1, 0);
            attron(A_REVERSE);
            addstr("SPACE: Next page | B: Previous page | ESC: Return to menu");
            for (int i = 59; i < COLS; i++) addch(' ');
            standend();

            refresh();
            need_redraw = 0;
        }

        // Wait for key press
        int ch = getch();
        if (ch == 27) {  // ESC
            break;
        } else if (ch == ' ') {  // SPACE - next page (with looping)
            page = (page + 1) % (max_page + 1);
            need_redraw = 1;
        } else if (ch == 'b' || ch == 'B') {  // B - previous page (with looping)
            page = (page - 1 + (max_page + 1)) % (max_page + 1);
            need_redraw = 1;
        }
        // Ignore other keys
    }
}
