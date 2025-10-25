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
            // Page 1: Wiring Diagram
            move(row++, 0);
            attron(A_REVERSE);
            addstr("SD CARD WIRING - SPI MODE");
            standend();
            row++;

            move(row++, 2);
            addstr("Connect SD card module to FPGA GPIO pins:");
            row++;

            // ASCII art SD card diagram
            move(row++, 4);
            addstr("         SD CARD                    FPGA PINS");
            move(row++, 4);
            addstr("    ┌────────────┐");
            move(row++, 4);
            addstr("    │  1 2 3 4 5 │                (Olimex iCE40HX8K)");
            move(row++, 4);
            addstr("    │  6 7 8 9   │");
            move(row++, 4);
            addstr("    └────────────┘");
            row++;

            move(row++, 4);
            addstr("Pin 1: CS   (Chip Select)    →  Pin C2  (SPI_CS)");
            move(row++, 4);
            addstr("Pin 2: DI   (Data In)        →  Pin B1  (SPI_MOSI)");
            move(row++, 4);
            addstr("Pin 3: VSS  (Ground)         →  GND");
            move(row++, 4);
            addstr("Pin 4: VDD  (Power +3.3V)    →  +3.3V");
            move(row++, 4);
            addstr("Pin 5: CLK  (Clock)          →  Pin F5  (SPI_SCK)");
            move(row++, 4);
            addstr("Pin 6: VSS  (Ground)         →  GND");
            move(row++, 4);
            addstr("Pin 7: DO   (Data Out)       →  Pin C1  (SPI_MISO)");
            move(row++, 4);
            addstr("Pin 8: NC   (Not Connected)");
            move(row++, 4);
            addstr("Pin 9: NC   (Not Connected)");
            row++;

            move(row++, 2);
            attron(A_REVERSE);
            addstr("IMPORTANT NOTES:");
            standend();
            move(row++, 4);
            addstr("• Use 3.3V power - most modern SD cards support 3.3V");
            move(row++, 4);
            addstr("• Some adapters require 5V for level shifters - check yours!");
            move(row++, 4);
            addstr("• Connect BOTH VSS pins (3 and 6) to GND for stability");
            move(row++, 4);
            addstr("• CS is active LOW (pulled low during communication)");
            move(row++, 4);
            addstr("• This adapter has NO card detect pin - use soft eject");

        } else if (page == 1) {
            // Page 2: Menu Options
            move(row++, 0);
            attron(A_REVERSE);
            addstr("MENU OPTIONS");
            standend();
            row++;

            move(row++, 2);
            attron(A_REVERSE);
            addstr("1. Detect SD Card");
            standend();
            move(row++, 4);
            addstr("Initialize SD card and mount FAT filesystem");
            move(row++, 4);
            addstr("Displays: Card type, capacity, volume label, free space");
            row++;

            move(row++, 2);
            attron(A_REVERSE);
            addstr("2. Card Information");
            standend();
            move(row++, 4);
            addstr("Display CID (Card ID) and CSD (Card Specific Data) registers");
            move(row++, 4);
            addstr("Shows: Manufacturer, product name, serial number, speed rating");
            row++;

            move(row++, 2);
            attron(A_REVERSE);
            addstr("3. Format Card (FAT32)");
            standend();
            move(row++, 4);
            addstr("⚠ WARNING: ERASES ALL DATA! Creates fresh FAT32 filesystem");
            move(row++, 4);
            addstr("Use this if card won't mount or has corrupted filesystem");
            row++;

            move(row++, 2);
            attron(A_REVERSE);
            addstr("4. File Browser");
            standend();
            move(row++, 4);
            addstr("Browse files and directories on SD card (to be implemented)");
            row++;

            move(row++, 2);
            attron(A_REVERSE);
            addstr("5. Create Test File");
            standend();
            move(row++, 4);
            addstr("Create test file for write/read verification (to be implemented)");

        } else if (page == 2) {
            // Page 3: Keyboard & Technical Info
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
            addstr("SPI SPEED CONFIGURATION");
            standend();
            move(row++, 4);
            addstr("SD cards support variable speeds:");
            move(row++, 4);
            addstr("  • 390 kHz    - Initialization (required by SD spec)");
            move(row++, 4);
            addstr("  • 12.5 MHz   - Default data transfer (reliable)");
            move(row++, 4);
            addstr("  • 25-50 MHz  - Maximum speed (may not work on all cards)");
            row++;

            move(row++, 0);
            attron(A_REVERSE);
            addstr("FILESYSTEM SUPPORT");
            standend();
            move(row++, 4);
            addstr("• FAT12, FAT16, FAT32 filesystems");
            move(row++, 4);
            addstr("• 8.3 filename format (no long filenames)");
            move(row++, 4);
            addstr("• Sector size: 512 bytes");
            move(row++, 4);
            addstr("• Max file size: 4 GB (FAT32 limit)");
            row++;

            move(row++, 0);
            attron(A_REVERSE);
            addstr("TROUBLESHOOTING");
            standend();
            move(row++, 4);
            addstr("Card not detected:");
            move(row++, 6);
            addstr("- Check wiring and power");
            move(row++, 6);
            addstr("- Try formatting card on PC first");
            move(row++, 6);
            addstr("- Some cards need slower SPI speeds");
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
