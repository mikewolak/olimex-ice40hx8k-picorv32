//==============================================================================
// Olimex iCE40HX8K-EVB RISC-V Platform
// SD Card Manager with FatFS
//
// Full-featured SD card file manager with interactive TUI
//
// Copyright (c) October 2025 Michael Wolak
// Email: mikewolak@gmail.com, mike@epromfoundry.com
//==============================================================================

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../../lib/incurses/curses.h"
#include "ff.h"
#include "sd_spi.h"
#include "hardware.h"
#include "io.h"
#include "help.h"

//==============================================================================
// FatFS Required Functions
//==============================================================================

// Get current time for FAT filesystem timestamps
// Since we have no RTC, return a fixed timestamp
DWORD get_fattime(void) {
    // Fixed date: January 1, 2025, 00:00:00
    // Bits 31-25: Year from 1980 (45 = 2025)
    // Bits 24-21: Month (1-12)
    // Bits 20-16: Day (1-31)
    // Bits 15-11: Hour (0-23)
    // Bits 10-5: Minute (0-59)
    // Bits 4-0: Second/2 (0-29)
    return ((DWORD)(2025 - 1980) << 25) | ((DWORD)1 << 21) | ((DWORD)1 << 16);
}

//==============================================================================
// Menu Options
//==============================================================================

#define MENU_DETECT_CARD    0
#define MENU_CARD_INFO      1
#define MENU_FORMAT_CARD    2
#define MENU_FILE_BROWSER   3
#define MENU_CREATE_FILE    4
#define MENU_BENCHMARK      5
#define MENU_SPI_SPEED      6
#define MENU_EJECT_CARD     7
#define NUM_MENU_OPTIONS    8

//==============================================================================
// Global State
//==============================================================================

static FATFS g_fs;              // FatFS filesystem object
static uint8_t g_card_mounted = 0;
static uint8_t g_card_detected = 0;
static uint32_t g_spi_speed = SPI_CLK_12MHZ;  // Default: 12.5 MHz

static const char *spi_speed_names[] = {
    "50.0 MHz", "25.0 MHz", "12.5 MHz", "6.25 MHz",
    "3.125 MHz", "1.562 MHz", "781 kHz", "390 kHz"
};

static const uint32_t spi_speeds[] = {
    SPI_CLK_50MHZ, SPI_CLK_25MHZ, SPI_CLK_12MHZ, SPI_CLK_6MHZ,
    SPI_CLK_3MHZ, SPI_CLK_1MHZ, SPI_CLK_781KHZ, SPI_CLK_390KHZ
};

//==============================================================================
// Status Display
//==============================================================================

void draw_status_bar(void) {
    move(LINES - 1, 0);
    attron(A_REVERSE);

    char status[128];
    snprintf(status, sizeof(status),
             " Card: %s | Mounted: %s | Speed: %s ",
             g_card_detected ? "DETECTED" : "NOT FOUND",
             g_card_mounted ? "YES" : "NO",
             spi_speed_names[g_spi_speed >> 2]);

    addstr(status);

    // Pad to end of line
    for (int i = strlen(status); i < COLS; i++) {
        addch(' ');
    }

    standend();
}

//==============================================================================
// Detect Card
//==============================================================================

void menu_detect_card(void) {
    clear();
    move(0, 0);
    attron(A_REVERSE);
    addstr("=== SD Card Detection ===");
    standend();

    move(2, 0);
    addstr("Initializing SD card...");
    refresh();

    uint8_t result = sd_init();

    move(4, 0);
    if (result == SD_OK) {
        g_card_detected = 1;
        attron(A_REVERSE);
        addstr("✓ SD Card detected successfully!");
        standend();

        move(6, 0);
        addstr("Card Type: ");
        switch (sd_get_card_type()) {
            case CARD_TYPE_SD1:  addstr("SD v1.x"); break;
            case CARD_TYPE_SD2:  addstr("SD v2.0 (SDSC)"); break;
            case CARD_TYPE_SDHC: addstr("SD v2.0 (SDHC/SDXC)"); break;
            default:             addstr("Unknown"); break;
        }

        move(7, 0);
        uint32_t sectors = sd_get_sector_count();
        uint32_t size_mb = (sectors / 2048);  // sectors * 512 / 1024 / 1024
        char buf[64];
        snprintf(buf, sizeof(buf), "Capacity: %lu MB (%lu sectors)",
                 (unsigned long)size_mb, (unsigned long)sectors);
        addstr(buf);

        // Try to mount filesystem
        move(9, 0);
        addstr("Mounting filesystem...");
        refresh();

        FRESULT fr = f_mount(&g_fs, "", 1);
        move(10, 0);
        if (fr == FR_OK) {
            g_card_mounted = 1;
            addstr("✓ Filesystem mounted successfully");

            // Get volume label
            char label[24];
            DWORD vsn;
            fr = f_getlabel("", label, &vsn);
            if (fr == FR_OK && label[0]) {
                move(11, 0);
                snprintf(buf, sizeof(buf), "Volume Label: %s", label);
                addstr(buf);
            }

            // Get free space
            FATFS *fs;
            DWORD fre_clust;
            fr = f_getfree("", &fre_clust, &fs);
            if (fr == FR_OK) {
                move(12, 0);
                DWORD total_sect = (fs->n_fatent - 2) * fs->csize;
                DWORD free_sect = fre_clust * fs->csize;
                snprintf(buf, sizeof(buf), "Free Space: %lu MB / %lu MB",
                        (unsigned long)(free_sect / 2048),
                        (unsigned long)(total_sect / 2048));
                addstr(buf);
            }
        } else {
            addstr("✗ Mount failed: ");
            addstr(sd_get_error_string(fr));
        }
    } else {
        g_card_detected = 0;
        attron(A_REVERSE);
        addstr("✗ No SD card detected or initialization failed");
        standend();

        move(6, 0);
        addstr("Error: ");
        addstr(sd_get_error_string(result));
    }

    move(LINES - 3, 0);
    addstr("Press any key to return to menu...");
    refresh();

    getch();
}

//==============================================================================
// Card Info
//==============================================================================

void menu_card_info(void) {
    clear();
    move(0, 0);
    attron(A_REVERSE);
    addstr("=== SD Card Information ===");
    standend();

    if (!g_card_detected) {
        move(2, 0);
        addstr("No card detected. Please run 'Detect Card' first.");
        move(LINES - 3, 0);
        addstr("Press any key to return...");
        refresh();
        getch();
        return;
    }

    // Display CID register
    move(2, 0);
    addstr("Card Identification (CID):");

    sd_cid_t cid;
    if (sd_read_cid(&cid) == SD_OK) {
        char buf[80];
        move(3, 2);
        snprintf(buf, sizeof(buf), "Manufacturer: 0x%02X", cid.mid);
        addstr(buf);

        move(4, 2);
        snprintf(buf, sizeof(buf), "OEM ID: %c%c", cid.oid[0], cid.oid[1]);
        addstr(buf);

        move(5, 2);
        snprintf(buf, sizeof(buf), "Product: %.5s", cid.pnm);
        addstr(buf);

        move(6, 2);
        snprintf(buf, sizeof(buf), "Revision: %d.%d", cid.prv >> 4, cid.prv & 0xF);
        addstr(buf);

        move(7, 2);
        snprintf(buf, sizeof(buf), "Serial: %08lX", (unsigned long)cid.psn);
        addstr(buf);

        move(8, 2);
        snprintf(buf, sizeof(buf), "Manufacture Date: %d/%04d", cid.mdt & 0xF, 2000 + (cid.mdt >> 4));
        addstr(buf);
    }

    // Display CSD register
    move(10, 0);
    addstr("Card Specific Data (CSD):");

    sd_csd_t csd;
    if (sd_read_csd(&csd) == SD_OK) {
        char buf[80];
        move(11, 2);
        snprintf(buf, sizeof(buf), "Max Transfer Rate: %d MB/s", csd.tran_speed);
        addstr(buf);

        move(12, 2);
        snprintf(buf, sizeof(buf), "Write Protect: %s", csd.wp ? "YES" : "NO");
        addstr(buf);
    }

    move(LINES - 3, 0);
    addstr("Press any key to return...");
    refresh();
    getch();
}

//==============================================================================
// Format Card
//==============================================================================

void menu_format_card(void) {
    clear();
    move(0, 0);
    attron(A_REVERSE);
    addstr("=== Format SD Card ===");
    standend();

    if (!g_card_detected) {
        move(2, 0);
        addstr("No card detected. Cannot format.");
        move(LINES - 3, 0);
        addstr("Press any key to return...");
        refresh();
        getch();
        return;
    }

    move(2, 0);
    attron(A_REVERSE);
    addstr("WARNING: This will ERASE ALL DATA on the SD card!");
    standend();

    move(4, 0);
    addstr("Are you sure? (y/N): ");
    refresh();

    int ch = getch();
    if (ch != 'y' && ch != 'Y') {
        return;
    }

    move(6, 0);
    addstr("Formatting... Please wait...");
    refresh();

    BYTE work[512];  // Work area for f_mkfs
    FRESULT fr = f_mkfs("", 0, work, sizeof(work));

    move(8, 0);
    if (fr == FR_OK) {
        addstr("✓ Format complete!");

        // Remount
        g_card_mounted = 0;
        fr = f_mount(&g_fs, "", 1);
        if (fr == FR_OK) {
            g_card_mounted = 1;
            move(9, 0);
            addstr("✓ Filesystem remounted");
        }
    } else {
        addstr("✗ Format failed: ");
        addstr(sd_get_error_string(fr));
    }

    move(LINES - 3, 0);
    addstr("Press any key to return...");
    refresh();
    getch();
}

//==============================================================================
// SPI Speed Configuration
//==============================================================================

void menu_spi_speed(void) {
    int selected = g_spi_speed >> 2;  // Convert speed to index
    int need_redraw = 1;

    while (1) {
        if (need_redraw) {
            clear();
            move(0, 0);
            attron(A_REVERSE);
            addstr("=== SPI Speed Configuration ===");
            standend();

            move(2, 0);
            addstr("Select SPI clock speed:");
            move(3, 0);
            addstr("(Higher speeds may not work with all cards)");

            for (int i = 0; i < 8; i++) {
                move(5 + i, 2);
                if (i == selected) {
                    attron(A_REVERSE);
                }
                char buf[64];
                snprintf(buf, sizeof(buf), "  %s  ", spi_speed_names[i]);
                addstr(buf);
                if (i == selected) {
                    standend();
                }
            }

            move(LINES - 3, 0);
            addstr("UP/DOWN: Navigate | ENTER: Select | ESC: Cancel");
            refresh();
            need_redraw = 0;
        }

        int ch = getch();

        if (ch == 27) {  // ESC
            break;
        } else if (ch == '\n' || ch == '\r') {
            g_spi_speed = spi_speeds[selected];
            sd_set_speed(g_spi_speed);
            break;
        } else if (ch == 65 || ch == 'k') {  // UP
            if (selected > 0) {
                selected--;
                need_redraw = 1;
            }
        } else if (ch == 66 || ch == 'j') {  // DOWN
            if (selected < 7) {
                selected++;
                need_redraw = 1;
            }
        }
    }
}

//==============================================================================
// Eject Card
//==============================================================================

void menu_eject_card(void) {
    clear();
    move(0, 0);
    attron(A_REVERSE);
    addstr("=== Eject SD Card ===");
    standend();

    move(2, 0);
    addstr("Unmounting filesystem...");
    refresh();

    if (g_card_mounted) {
        f_mount(NULL, "", 0);
        g_card_mounted = 0;
    }

    move(3, 0);
    addstr("✓ Card ejected safely");

    g_card_detected = 0;

    move(5, 0);
    addstr("You can now safely remove the SD card.");

    move(LINES - 3, 0);
    addstr("Press any key to return...");
    refresh();
    getch();
}

//==============================================================================
// Main Menu
//==============================================================================

int main(void) {
    int selected_menu = MENU_DETECT_CARD;
    int old_selected = -1;
    int need_full_redraw = 1;

    // Initialize ncurses
    initscr();
    noecho();
    raw();
    keypad(stdscr, TRUE);
    curs_set(0);

    // Initialize SPI
    sd_spi_init();

    while (1) {
        if (need_full_redraw || old_selected != selected_menu) {
            clear();

            // Header
            move(0, 0);
            attron(A_REVERSE);
            addstr("       SD CARD MANAGER - PicoRV32 FPGA Platform       ");
            for (int i = 54; i < COLS; i++) addch(' ');
            standend();

            // Menu
            const int menu_row = 3;
            move(menu_row, 2);
            addstr("Main Menu:");

            const char *menu_items[] = {
                "Detect SD Card",
                "Card Information",
                "Format Card (FAT32)",
                "File Browser",
                "Create Test File",
                "Read/Write Benchmark",
                "SPI Speed Configuration",
                "Eject Card"
            };

            // Remove old selection highlight
            if (old_selected >= 0 && old_selected != selected_menu) {
                move(menu_row + 2 + old_selected, 0);
                clrtoeol();
                addstr("   ");
                addstr(menu_items[old_selected]);
            }

            // Draw all menu items
            for (int i = 0; i < NUM_MENU_OPTIONS; i++) {
                move(menu_row + 2 + i, 0);
                if (i == selected_menu) {
                    attron(A_REVERSE);
                    addstr(" > ");
                } else {
                    addstr("   ");
                }
                addstr(menu_items[i]);
                if (i == selected_menu) {
                    standend();
                }
            }

            old_selected = selected_menu;
            need_full_redraw = 0;
        }

        // Help hint
        move(LINES - 2, 0);
        addstr("Press 'H' for Help with wiring diagram");
        clrtoeol();

        // Status bar
        draw_status_bar();
        refresh();

        // Handle input
        int ch = getch();

        if (ch == 'q' || ch == 'Q') {
            break;
        } else if (ch == 'h' || ch == 'H') {  // HELP
            show_help();
            need_full_redraw = 1;
        } else if (ch == 65 || ch == 'k') {  // UP
            if (selected_menu > 0) {
                selected_menu--;
            }
        } else if (ch == 66 || ch == 'j') {  // DOWN
            if (selected_menu < NUM_MENU_OPTIONS - 1) {
                selected_menu++;
            }
        } else if (ch == '\n' || ch == '\r') {  // ENTER
            // Execute selected menu option
            switch (selected_menu) {
                case MENU_DETECT_CARD:
                    menu_detect_card();
                    break;
                case MENU_CARD_INFO:
                    menu_card_info();
                    break;
                case MENU_FORMAT_CARD:
                    menu_format_card();
                    break;
                case MENU_FILE_BROWSER:
                    // TODO: Implement file browser
                    break;
                case MENU_CREATE_FILE:
                    // TODO: Implement create test file
                    break;
                case MENU_BENCHMARK:
                    // TODO: Implement benchmark
                    break;
                case MENU_SPI_SPEED:
                    menu_spi_speed();
                    break;
                case MENU_EJECT_CARD:
                    menu_eject_card();
                    break;
            }
            need_full_redraw = 1;
        }
    }

    // Cleanup
    if (g_card_mounted) {
        f_mount(NULL, "", 0);
    }

    endwin();
    return 0;
}
