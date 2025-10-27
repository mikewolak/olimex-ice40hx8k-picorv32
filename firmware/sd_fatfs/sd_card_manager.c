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
#include "overlay_upload.h"
#include "overlay_loader.h"
#include "file_browser.h"
#include "crash_dump.h"

//==============================================================================
// Timer Functions (copied from spi_test.c)
//==============================================================================

#define TIMER_BASE          0x80000020
#define TIMER_CR            (*(volatile uint32_t*)(TIMER_BASE + 0x00))
#define TIMER_SR            (*(volatile uint32_t*)(TIMER_BASE + 0x04))
#define TIMER_PSC           (*(volatile uint32_t*)(TIMER_BASE + 0x08))
#define TIMER_ARR           (*(volatile uint32_t*)(TIMER_BASE + 0x0C))
#define TIMER_CNT           (*(volatile uint32_t*)(TIMER_BASE + 0x10))

#define TIMER_CR_ENABLE     (1 << 0)
#define TIMER_SR_UIF        (1 << 0)

static void timer_init_bench(void) {
    TIMER_CR = 0;               // Disable timer
    TIMER_SR = TIMER_SR_UIF;    // Clear any pending interrupt
}

static void timer_config_bench(uint16_t psc, uint32_t arr) {
    TIMER_PSC = psc;
    TIMER_ARR = arr;
}

static void timer_start_bench(void) {
    TIMER_CR = TIMER_CR_ENABLE;  // Enable, continuous mode
}

static void timer_stop_bench(void) {
    TIMER_CR = 0;  // Disable timer
}

static void timer_clear_irq_bench(void) {
    TIMER_SR = TIMER_SR_UIF;     // Write 1 to clear
}

//==============================================================================
// FatFS Error Code to String Conversion
//==============================================================================

static const char* fresult_to_string(FRESULT fr) {
    switch (fr) {
        case FR_OK:                  return "Success";
        case FR_DISK_ERR:            return "Disk I/O error";
        case FR_INT_ERR:             return "CRC mismatch - data integrity error";
        case FR_NOT_READY:           return "Drive not ready";
        case FR_NO_FILE:             return "File not found";
        case FR_NO_PATH:             return "Path not found";
        case FR_INVALID_NAME:        return "Invalid path name";
        case FR_DENIED:              return "Access denied";
        case FR_EXIST:               return "File/directory already exists";
        case FR_INVALID_OBJECT:      return "Invalid file/directory object";
        case FR_WRITE_PROTECTED:     return "Write protected";
        case FR_INVALID_DRIVE:       return "Invalid drive number";
        case FR_NOT_ENABLED:         return "No work area";
        case FR_NO_FILESYSTEM:       return "No valid FAT filesystem";
        case FR_MKFS_ABORTED:        return "mkfs aborted";
        case FR_TIMEOUT:             return "Timeout";
        case FR_LOCKED:              return "File locked";
        case FR_NOT_ENOUGH_CORE:     return "Not enough memory";
        case FR_TOO_MANY_OPEN_FILES: return "Too many open files";
        case FR_INVALID_PARAMETER:   return "Protocol error - invalid data received";
        default:                     return "Unknown error";
    }
}

// Performance tracking for benchmark
volatile uint32_t bytes_transferred_this_second = 0;
volatile uint32_t bytes_per_second = 0;
volatile uint8_t timer_tick_flag = 0;

// Function pointer for overlay timer interrupt handler
// Overlays can set this to their timer handler function
volatile void (*overlay_timer_irq_handler)(void) = 0;

// External crash context (filled by assembly IRQ wrapper in start.S)
extern crash_context_t g_crash_context;

// IRQ control functions (from spi_test.c)
static inline void irq_setmask(uint32_t mask) {
    uint32_t dummy;
    __asm__ volatile (".insn r 0x0B, 6, 3, %0, %1, x0" : "=r"(dummy) : "r"(mask));
}

// Interrupt handler (called from start.S)
// This overrides the weak irq_handler symbol
void irq_handler(uint32_t irqs) {
    if (irqs & (1 << 0)) {  // Timer interrupt (IRQ[0])
        // Check if this is a watchdog timeout (one-shot mode)
        if (TIMER_CR & (1 << 2)) {  // TIMER_ONE_SHOT bit set
            // This is a watchdog timeout - overlay hung!
            TIMER_SR = TIMER_SR_UIF;  // Clear interrupt
            TIMER_CR = 0;             // Disable timer

            // Read PC from q2
            uint32_t pc;
            __asm__ volatile (".insn r 0x0B, 4, 0, %0, x2, x0" : "=r"(pc));

            // 3 fast blinks to show watchdog triggered
            for (int i = 0; i < 3; i++) {
                LED_REG = 0x03;  // Both on
                for (volatile int j = 0; j < 500000; j++);
                LED_REG = 0x00;  // Both off
                for (volatile int j = 0; j < 500000; j++);
            }

            // Toggle LEDs back and forth 100 times (clearly visible)
            for (int i = 0; i < 100; i++) {
                LED_REG = 0x01;  // LED1 only
                for (volatile int j = 0; j < 500000; j++);
                LED_REG = 0x02;  // LED2 only
                for (volatile int j = 0; j < 500000; j++);
            }

            // Halt with both LEDs on
            while (1) {
                LED_REG = 0x03;  // Both LEDs on = done
            }
        } else {
            // This is a normal continuous timer tick
            timer_clear_irq_bench();

            // Call overlay timer handler if one is registered
            if (overlay_timer_irq_handler) {
                overlay_timer_irq_handler();
            }

            // Update bytes_per_second (average over last second)
            bytes_per_second = bytes_transferred_this_second;

            // Reset for next measurement period
            bytes_transferred_this_second = 0;

            // Set flag to notify main loop
            timer_tick_flag = 1;
        }
    }
}

//==============================================================================
// Utility Functions
//==============================================================================

// Format bytes/sec with auto-adjusting units (B/s, KB/s, MB/s)
void format_bytes_per_sec(uint32_t bytes_per_sec, char *buf, int buf_size) {
    if (bytes_per_sec >= 1000000) {
        // MB/s (1,000,000+ bytes/sec)
        uint32_t mb = bytes_per_sec / 1000000;
        uint32_t frac = (bytes_per_sec % 1000000) / 100000;  // One decimal place
        snprintf(buf, buf_size, "%u.%u MB/s", (unsigned int)mb, (unsigned int)frac);
    } else if (bytes_per_sec >= 1000) {
        // KB/s (1,000+ bytes/sec)
        uint32_t kb = bytes_per_sec / 1000;
        uint32_t frac = (bytes_per_sec % 1000) / 100;  // One decimal place
        snprintf(buf, buf_size, "%u.%u KB/s", (unsigned int)kb, (unsigned int)frac);
    } else {
        // B/s (< 1000 bytes/sec)
        snprintf(buf, buf_size, "%u B/s", (unsigned int)bytes_per_sec);
    }
}

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
#define MENU_UPLOAD_OVERLAY 4
#define MENU_BROWSE_OVERLAYS 5
#define MENU_UPLOAD_EXEC    6
#define MENU_CREATE_FILE    7
#define MENU_BENCHMARK      8
#define MENU_SPI_SPEED      9
#define MENU_EJECT_CARD     10
#define NUM_MENU_OPTIONS    11

//==============================================================================
// Global State
//==============================================================================

static FATFS g_fs;              // FatFS filesystem object
uint8_t g_card_mounted = 0;  // Global - used by file_browser.c
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
// Arrow Key Helper - Detects ESC [ A/B sequences from arrow keys
//==============================================================================

int get_key_with_arrows(void) {
    int ch = getch();

    if (ch == 27) {  // ESC - could be arrow key sequence or just ESC
        // Check if this is an arrow key (ESC [ X) or just ESC
        timeout(10);  // Brief timeout to check for following characters
        int ch2 = getch();
        if (ch2 == '[') {
            int ch3 = getch();
            timeout(-1);
            if (ch3 == 'A') return KEY_UP;       // Up arrow
            else if (ch3 == 'B') return KEY_DOWN;  // Down arrow
            else if (ch3 == 'C') return KEY_RIGHT; // Right arrow
            else if (ch3 == 'D') return KEY_LEFT;  // Left arrow
            else return 27;  // Unknown escape sequence, return ESC
        } else {
            timeout(-1);
            return 27;  // Just ESC key
        }
    }

    return ch;  // Normal character
}

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

        // Test if we can actually read from the card
        uint8_t test_block[512];
        uint8_t test_result = sd_read_block(0, test_block);

        move(10, 0);
        if (test_result != SD_OK) {
            char errbuf[64];
            snprintf(errbuf, sizeof(errbuf), "✗ Cannot read sector 0, error=%d", test_result);
            addstr(errbuf);
            refresh();

            // Don't try to mount if we can't read
            move(LINES - 3, 0);
            addstr("Press any key to return to menu...");
            refresh();
            timeout(-1);
            while (getch() == ERR);
            return;
        } else {
            snprintf(buf, sizeof(buf), "✓ Sector 0 readable, sig=0x%02X%02X", test_block[511], test_block[510]);
            addstr(buf);
        }
        refresh();  // Display the sector read result before mounting

        move(11, 0);
        addstr("Calling f_mount...");
        refresh();

        FRESULT fr = f_mount(&g_fs, "", 1);
        move(12, 0);
        if (fr == FR_OK) {
            g_card_mounted = 1;
            addstr("✓ Filesystem mounted successfully");

            // Get volume label
            char label[24];
            DWORD vsn;
            fr = f_getlabel("", label, &vsn);
            if (fr == FR_OK && label[0]) {
                move(13, 0);
                snprintf(buf, sizeof(buf), "Volume Label: %s", label);
                addstr(buf);
            }

            // Get free space
            FATFS *fs;
            DWORD fre_clust;
            fr = f_getfree("", &fre_clust, &fs);
            if (fr == FR_OK) {
                move(14, 0);
                DWORD total_sect = (fs->n_fatent - 2) * fs->csize;
                DWORD free_sect = fre_clust * fs->csize;
                snprintf(buf, sizeof(buf), "Free Space: %lu MB / %lu MB",
                        (unsigned long)(free_sect / 2048),
                        (unsigned long)(total_sect / 2048));
                addstr(buf);
            }
        } else {
            char errbuf[64];
            snprintf(errbuf, sizeof(errbuf), "✗ Mount failed: FRESULT=%d", (int)fr);
            addstr(errbuf);
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

    timeout(-1);
    while (getch() == ERR);  // Loop until we get a real key (incurses returns ERR when no key available)  // Loop until we get a real key (not ERR)
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
    refresh();

    if (!g_card_detected) {
        move(2, 0);
        addstr("No card detected. Please run 'Detect Card' first.");
        move(LINES - 3, 0);
        addstr("Press any key to return...");
        refresh();

        timeout(-1);
        while (getch() == ERR);
        return;
    }

    // Display CID register
    move(2, 0);
    addstr("Card Identification (CID):");
    refresh();

    sd_cid_t cid;
    uint8_t cid_result = sd_read_cid(&cid);
    if (cid_result == SD_OK) {
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
    } else {
        move(3, 2);
        addstr("Error: Failed to read CID register");
        char errstr[32];
        snprintf(errstr, sizeof(errstr), "Error code: %d", cid_result);
        move(4, 2);
        addstr(errstr);
    }
    refresh();

    // Display CSD register
    move(10, 0);
    addstr("Card Specific Data (CSD):");
    refresh();

    sd_csd_t csd;
    uint8_t csd_result = sd_read_csd(&csd);
    if (csd_result == SD_OK) {
        char buf[80];
        move(11, 2);
        snprintf(buf, sizeof(buf), "Max Transfer Rate: %d MB/s", csd.tran_speed);
        addstr(buf);

        move(12, 2);
        snprintf(buf, sizeof(buf), "Write Protect: %s", csd.wp ? "YES" : "NO");
        addstr(buf);
    } else {
        move(11, 2);
        addstr("Error: Failed to read CSD register");
        char errstr[32];
        snprintf(errstr, sizeof(errstr), "Error code: %d", csd_result);
        move(12, 2);
        addstr(errstr);
    }
    refresh();

    move(LINES - 3, 0);
    addstr("Press any key to return...");
    refresh();

    timeout(-1);
    while (getch() == ERR);  // Loop until we get a real key (incurses returns ERR when no key available)
}

//==============================================================================
// Format Card
//==============================================================================

//==============================================================================
// Format Card - Advanced Menu
//==============================================================================

void menu_format_card(void) {
    // Flush any pending input before starting
    timeout(0);
    while (getch() != ERR);
    timeout(-1);

    // Format options
    const char* fs_types[] = {
        "FAT (auto-detect FAT12/16/32)",
        "FAT32 (recommended for <32GB)",
        "exFAT (for >32GB cards)"
    };
    const BYTE fs_opts[] = {FM_FAT | FM_SFD, FM_FAT32 | FM_SFD, FM_EXFAT | FM_SFD};

    const char* part_types[] = {
        "No partition table (simple format)",
        "MBR partition table (recommended)",
        "GPT partition table (exFAT only)"
    };

    int selected_fs = 1;  // Default: FAT32
    int selected_part = 0;  // Default: No partition table (super floppy)
    int au_size = 0;  // Auto allocation unit
    int current_menu = 0;  // 0=fs type, 1=partition type, 2=confirm
    int need_redraw = 1;

    if (!g_card_detected) {
        clear();
        move(0, 0);
        attron(A_REVERSE);
        addstr("=== Format SD Card ===");
        standend();
        move(2, 0);
        addstr("No card detected. Cannot format.");
        move(LINES - 3, 0);
        addstr("Press any key to return...");
        refresh();
        timeout(-1);
        while (getch() == ERR);
        return;
    }

    while (1) {
        if (need_redraw) {
            clear();
            move(0, 0);
            attron(A_REVERSE);
            addstr("=== Advanced SD Card Formatter ===");
            standend();

            // Card info
            move(2, 0);
            uint32_t sectors = sd_get_sector_count();
            uint32_t size_mb = (sectors / 2048);
            char buf[80];
            snprintf(buf, sizeof(buf), "Card: %lu MB (%lu sectors)",
                    (unsigned long)size_mb, (unsigned long)sectors);
            addstr(buf);

            // Filesystem type selection
            move(4, 0);
            if (current_menu == 0) attron(A_REVERSE);
            addstr("[ Filesystem Type ]");
            if (current_menu == 0) standend();

            for (int i = 0; i < 3; i++) {
                move(5 + i, 2);
                if (current_menu == 0 && i == selected_fs) {
                    addstr("> ");
                    attron(A_REVERSE);
                } else {
                    addstr("  ");
                }
                addstr(fs_types[i]);
                if (current_menu == 0 && i == selected_fs) {
                    standend();
                }
            }

            // Partition type selection
            move(9, 0);
            if (current_menu == 1) attron(A_REVERSE);
            addstr("[ Partition Table ]");
            if (current_menu == 1) standend();

            for (int i = 0; i < 3; i++) {
                move(10 + i, 2);
                if (current_menu == 1 && i == selected_part) {
                    addstr("> ");
                    attron(A_REVERSE);
                } else {
                    addstr("  ");
                }
                addstr(part_types[i]);
                if (current_menu == 1 && i == selected_part) {
                    standend();
                }
            }

            // Warning
            move(14, 0);
            attron(A_REVERSE);
            addstr("WARNING: This will ERASE ALL DATA on the card!");
            standend();

            // Instructions
            move(LINES - 4, 0);
            if (current_menu < 2) {
                addstr("UP/DOWN: Select | TAB: Next | ENTER: Format | ESC: Cancel");
            } else {
                addstr("Press 'Y' to confirm format, any other key to cancel");
            }

            refresh();
            need_redraw = 0;
        }

        timeout(-1);
        int ch;

        if (current_menu < 2) {
            // Navigation mode - use arrow key helper
            ch = get_key_with_arrows();
        } else {
            // Confirmation mode - use plain getch() for simplicity
            ch = getch();
        }

        if (current_menu < 2) {
            // Navigation mode
            if (ch == 27) {  // ESC
                return;
            } else if (ch == '\t' || ch == 9) {  // TAB
                current_menu = (current_menu + 1) % 2;
                need_redraw = 1;
            } else if (ch == KEY_UP || ch == 'k' || ch == 'K') {  // UP (arrow or k/K)
                if (current_menu == 0 && selected_fs > 0) {
                    selected_fs--;
                    need_redraw = 1;
                } else if (current_menu == 1 && selected_part > 0) {
                    selected_part--;
                    need_redraw = 1;
                }
            } else if (ch == KEY_DOWN || ch == 'j' || ch == 'J') {  // DOWN (arrow or j/J)
                if (current_menu == 0 && selected_fs < 2) {
                    selected_fs++;
                    need_redraw = 1;
                } else if (current_menu == 1 && selected_part < 2) {
                    selected_part++;
                    need_redraw = 1;
                }
            } else if (ch == '\n' || ch == '\r') {  // ENTER - go to confirm
                current_menu = 2;
                need_redraw = 1;
            }
        } else {
            // Confirmation mode (current_menu == 2)
            // DEBUG: Show what key was pressed
            move(LINES - 2, 0);
            clrtoeol();
            char debug[64];
            snprintf(debug, sizeof(debug), "[DEBUG: key=%d (0x%02X) '%c']", ch, ch,
                    (ch >= 32 && ch < 127) ? ch : '?');
            addstr(debug);
            refresh();

            if (ch == 'y' || ch == 'Y') {
                // Perform format
                break;
            } else {
                // Cancel - but wait for another key so we can see the debug
                move(LINES - 1, 0);
                addstr("Format cancelled. Press any key...");
                refresh();
                timeout(-1);
                getch();
                return;
            }
        }
    }

    // Format confirmed - do the actual format
    clear();
    move(0, 0);
    attron(A_REVERSE);
    addstr("=== Formatting SD Card ===");
    standend();

    char buf[80];
    move(2, 0);
    snprintf(buf, sizeof(buf), "Filesystem: %s", fs_types[selected_fs]);
    addstr(buf);
    move(3, 0);
    snprintf(buf, sizeof(buf), "Partition:  %s", part_types[selected_part]);
    addstr(buf);
    refresh();

    // Prepare format options structure
    MKFS_PARM fmt_opt;
    fmt_opt.fmt = fs_opts[selected_fs];
    fmt_opt.n_fat = 1;  // Number of FAT copies
    fmt_opt.align = 0;  // Data area alignment (0=auto)
    fmt_opt.n_root = 0;  // Number of root directory entries (0=auto)
    fmt_opt.au_size = au_size;  // Allocation unit size (0=auto)

    // Adjust for partition type
    if (selected_part == 0) {
        fmt_opt.fmt |= FM_SFD;  // Super floppy disk (no partition table)
    } else if (selected_part == 2) {
        fmt_opt.fmt |= 0x08;  // GPT
    }

    // Work area for f_mkfs (needs to be large enough)
    static BYTE work[4096];

    // Show progress starting
    move(5, 0);
    addstr("Progress:");
    move(6, 1);
    addstr("[                                                  ]");
    move(7, 0);
    addstr("Status: Formatting...");
    move(8, 0);
    addstr("  0%");
    refresh();

    // Call f_mkfs - this will take time
    FRESULT fr = f_mkfs("", &fmt_opt, work, sizeof(work));

    // Show result
    move(7, 8);
    clrtoeol();
    if (fr == FR_OK) {
        addstr("Complete!           ");

        // Draw full progress bar
        move(6, 2);
        for (int i = 0; i < 50; i++) {
            addch('=');
        }
        move(8, 0);
        addstr("100%");
        refresh();

        // Try to remount
        move(10, 0);
        addstr("Remounting filesystem...");
        refresh();

        g_card_mounted = 0;
        fr = f_mount(&g_fs, "", 1);
        if (fr == FR_OK) {
            g_card_mounted = 1;
            move(11, 0);
            addstr("✓ Filesystem mounted successfully");
        } else {
            move(11, 0);
            snprintf(buf, sizeof(buf), "✗ Mount failed: FRESULT=%d", (int)fr);
            addstr(buf);
        }
    } else {
        snprintf(buf, sizeof(buf), "Failed: FRESULT=%d", (int)fr);
        addstr(buf);
        move(9, 0);
        addstr("Possible causes:");
        move(10, 2);
        addstr("- Card is write-protected");
        move(11, 2);
        addstr("- Card is too large for selected filesystem");
        move(12, 2);
        addstr("- Hardware error");
    }

    move(LINES - 3, 0);
    addstr("Press any key to return...");
    refresh();
    timeout(-1);
    while (getch() == ERR);
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
                move(5 + i, 0);
                if (i == selected) {
                    addstr(" > ");
                    attron(A_REVERSE);
                } else {
                    addstr("   ");
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

        timeout(-1);
        int ch = get_key_with_arrows();  // Use helper to detect arrow keys

        if (ch == 27) {  // ESC
            break;
        } else if (ch == '\n' || ch == '\r') {
            g_spi_speed = spi_speeds[selected];
            sd_set_speed(g_spi_speed);
            break;
        } else if (ch == KEY_UP || ch == 'k' || ch == 'K') {  // UP (arrow or k/K)
            if (selected > 0) {
                selected--;
                need_redraw = 1;
            }
        } else if (ch == KEY_DOWN || ch == 'j' || ch == 'J') {  // DOWN (arrow or j/J)
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

    timeout(-1);
    while (getch() == ERR);  // Loop until we get a real key (incurses returns ERR when no key available)
}

//==============================================================================
// Upload Overlay
//==============================================================================

void menu_upload_overlay(void) {
    clear();
    move(0, 0);
    attron(A_REVERSE);
    addstr("=== Upload Overlay via UART ===");
    standend();

    move(2, 0);

    // Check if card is mounted
    if (!g_card_mounted) {
        addstr("Error: SD card not mounted!");
        move(4, 0);
        addstr("Please detect and mount card first (Menu option 1).");
        move(LINES - 3, 0);
        addstr("Press any key to return to menu...");
        refresh();

        timeout(-1);
        while (getch() == ERR);
        return;
    }

    addstr("This will receive an overlay binary via UART and save it to the SD card.");
    move(4, 0);
    addstr("Protocol: FAST streaming (use fw_upload_fast tool)");
    move(5, 0);
    addstr("Maximum size: 128 KB");

    move(7, 0);
    addstr("Enter overlay filename (e.g., HEXEDIT.BIN): ");
    refresh();

    // Get filename from user
    char filename[32];
    echo();
    curs_set(1);

    // Read filename
    unsigned int idx = 0;
    while (1) {
        int ch = getch();
        if (ch == '\n' || ch == '\r') {
            filename[idx] = '\0';
            break;
        } else if (ch == 127 || ch == '\b') {  // Backspace
            if (idx > 0) {
                idx--;
                move(7, 45 + idx);
                addch(' ');
                move(7, 45 + idx);
            }
        } else if (idx < sizeof(filename) - 1 && ch >= 32 && ch < 127) {
            filename[idx++] = ch;
            addch(ch);
        }
        refresh();
    }

    noecho();
    curs_set(0);

    // Validate filename
    if (idx == 0) {
        move(9, 0);
        addstr("Error: Filename cannot be empty!");
        move(LINES - 3, 0);
        addstr("Press any key to return to menu...");
        refresh();

        timeout(-1);
        while (getch() == ERR);
        return;
    }

    move(9, 0);
    addstr("Filename: ");
    addstr(filename);

    move(11, 0);
    addstr("Ready to receive overlay...");
    move(12, 0);
    addstr("Start upload from PC now using:");
    move(13, 0);
    addstr("  fw_upload_fast -p /dev/ttyUSB0 overlay.bin");

    move(15, 0);
    refresh();

    // Exit ncurses temporarily for upload (direct UART access)
    endwin();

    // Call upload function
    FRESULT fr = overlay_upload(filename);

    // Restore ncurses
    refresh();

    // Show result
    clear();
    move(0, 0);
    attron(A_REVERSE);
    addstr("=== Upload Result ===");
    standend();

    move(2, 0);
    if (fr == FR_OK) {
        attron(A_REVERSE);
        addstr("✓ SUCCESS! Overlay uploaded and saved to SD card.");
        standend();

        move(4, 0);
        char path[64];
        snprintf(path, sizeof(path), "%s/%s", OVERLAY_DIR, filename);
        addstr("File: ");
        addstr(path);
    } else {
        attron(A_REVERSE);
        addstr("✗ FAILED! Upload error.");
        standend();

        move(4, 0);
        addstr("Error: ");
        addstr(fresult_to_string(fr));

        move(5, 0);
        char errstr[32];
        snprintf(errstr, sizeof(errstr), "(Error code: %d)", fr);
        addstr(errstr);
    }

    move(LINES - 3, 0);
    addstr("Press any key to return to menu...");
    refresh();

    timeout(-1);
    while (getch() == ERR);  // Loop until we get a real key (incurses returns ERR when no key available)
}

//==============================================================================
// Upload and Execute (Direct RAM, No SD Card)
//==============================================================================

void menu_upload_and_execute(void) {
    clear();
    move(0, 0);
    attron(A_REVERSE);
    addstr("=== Upload & Execute (Direct RAM) ===");
    standend();

    move(2, 0);
    addstr("This will upload an overlay via UART and execute it immediately");
    move(3, 0);
    addstr("WITHOUT saving to SD card.");

    move(5, 0);
    addstr("Protocol: FAST streaming (use fw_upload_fast tool)");
    move(6, 0);
    addstr("Maximum size: 128 KB");

    move(8, 0);
    addstr("Ready to receive overlay...");
    move(9, 0);
    addstr("Start upload from PC now using:");
    move(10, 0);
    addstr("  fw_upload_fast -p /dev/ttyUSB0 overlay.bin");

    move(12, 0);
    refresh();

    // Exit ncurses temporarily for upload and execution
    endwin();

    // Call upload and execute function
    FRESULT fr = overlay_upload_and_execute();

    // Restore ncurses
    refresh();

    // Show result
    clear();
    move(0, 0);
    attron(A_REVERSE);
    addstr("=== Upload & Execute Result ===");
    standend();

    move(2, 0);
    if (fr == FR_OK) {
        attron(A_REVERSE);
        addstr("✓ SUCCESS! Overlay uploaded and executed.");
        standend();
    } else {
        attron(A_REVERSE);
        addstr("✗ FAILED! Upload or execution error.");
        standend();

        move(4, 0);
        addstr("Error: ");
        addstr(fresult_to_string(fr));

        move(5, 0);
        char errstr[32];
        snprintf(errstr, sizeof(errstr), "(Error code: %d)", fr);
        addstr(errstr);
    }

    move(LINES - 3, 0);
    addstr("Press any key to return to menu...");
    refresh();

    timeout(-1);
    while (getch() == ERR);  // Loop until we get a real key (incurses returns ERR when no key available)
}

//==============================================================================
// Browse and Run Overlays
//==============================================================================

void menu_browse_overlays(void) {
    overlay_list_t list;
    FRESULT fr;
    int selected = 0;
    int need_redraw = 1;

    clear();
    move(0, 0);
    attron(A_REVERSE);
    addstr("=== Browse Overlays ===");
    standend();

    move(2, 0);

    // Check if card is mounted
    if (!g_card_mounted) {
        addstr("Error: SD card not mounted!");
        move(4, 0);
        addstr("Please detect and mount card first (Menu option 1).");
        move(LINES - 3, 0);
        addstr("Press any key to return to menu...");
        refresh();

        timeout(-1);
        while (getch() == ERR);
        return;
    }

    // Browse overlays on SD card
    addstr("Scanning /OVERLAYS directory...");
    refresh();

    fr = overlay_browse(&list);

    if (fr != FR_OK) {
        move(4, 0);
        addstr("Error: Cannot read /OVERLAYS directory");
        move(5, 0);
        addstr("Error code: ");
        char errstr[16];
        snprintf(errstr, sizeof(errstr), "%d", fr);
        addstr(errstr);
        move(LINES - 3, 0);
        addstr("Press any key to return to menu...");
        refresh();

        timeout(-1);
        while (getch() == ERR);
        return;
    }

    if (list.count == 0) {
        move(4, 0);
        addstr("No overlays found in /OVERLAYS directory");
        move(6, 0);
        addstr("Upload an overlay first (Menu option 4)");
        move(LINES - 3, 0);
        addstr("Press any key to return to menu...");
        refresh();

        timeout(-1);
        while (getch() == ERR);
        return;
    }

    // Interactive overlay selection
    while (1) {
        if (need_redraw) {
            clear();
            move(0, 0);
            attron(A_REVERSE);
            addstr("=== Browse Overlays ===");
            standend();

            move(2, 0);
            char buf[64];
            snprintf(buf, sizeof(buf), "Found %d overlay%s:",
                     list.count, list.count == 1 ? "" : "s");
            addstr(buf);

            // Display overlay list
            for (int i = 0; i < list.count; i++) {
                move(4 + i, 2);
                if (i == selected) {
                    attron(A_REVERSE);
                }

                overlay_info_t *info = &list.overlays[i];
                snprintf(buf, sizeof(buf), "  %-20s  %6lu bytes  ",
                         info->filename,
                         (unsigned long)info->size);
                addstr(buf);

                if (i == selected) {
                    standend();
                }
            }

            move(LINES - 3, 0);
            addstr("UP/DOWN: Navigate | ENTER: Load & Run | ESC: Back");
            refresh();
            need_redraw = 0;
        }

        timeout(-1);
        int ch = get_key_with_arrows();  // Use helper to detect arrow keys

        if (ch == 27) {  // ESC
            break;
        } else if (ch == '\n' || ch == '\r') {  // ENTER - Load and run overlay
            overlay_info_t *info = &list.overlays[selected];

            // Exit ncurses temporarily for overlay execution
            endwin();

            printf("\n");
            printf("========================================\n");
            printf("Loading overlay: %s\n", info->filename);
            printf("========================================\n");

            // Load overlay to execution address
            overlay_info_t loaded_info;
            fr = overlay_load(info->filename, OVERLAY_EXEC_BASE, &loaded_info);

            if (fr != FR_OK) {
                printf("\nError: Failed to load overlay (error %d)\n", fr);
                printf("Press any key to return to menu...\n");
                getch();
            } else {
                // Execute overlay
                overlay_execute(loaded_info.entry_point);

                // Overlay returned
                printf("\nPress any key to return to menu...\n");
                getch();
            }

            // Restore ncurses
            refresh();
            need_redraw = 1;

        } else if (ch == KEY_UP || ch == 'k' || ch == 'K') {  // UP (arrow or k/K)
            if (selected > 0) {
                selected--;
                need_redraw = 1;
            }
        } else if (ch == KEY_DOWN || ch == 'j' || ch == 'J') {  // DOWN (arrow or j/J)
            if (selected < list.count - 1) {
                selected++;
                need_redraw = 1;
            }
        }
    }
}

//==============================================================================
// Create Test File
//==============================================================================

void menu_create_test_file(void) {
    // EXACT pattern from help.c
    flushinp();
    timeout(-1);

    clear();
    move(0, 0);
    attron(A_REVERSE);
    addstr("=== Create Test File ===");
    standend();
    refresh();

    if (!g_card_mounted) {
        move(2, 0);
        addstr("Error: SD card not mounted!");
        move(4, 0);
        addstr("Please detect and mount card first (Menu option 1).");
        move(LINES - 3, 0);
        addstr("Press any key to return to menu...");
        refresh();
        timeout(-1);
        while (getch() == ERR);
        return;
    }

    // Fixed test parameters (like card_info does - no user input!)
    const char *filename = "TEST.TXT";
    const uint32_t size_kb = 100;  // 100 KB test file

    move(2, 0);
    addstr("Creating test file with fixed parameters:");
    move(3, 2);
    char buf[64];
    snprintf(buf, sizeof(buf), "Filename: %s", filename);
    addstr(buf);
    move(4, 2);
    snprintf(buf, sizeof(buf), "Size: %lu KB", (unsigned long)size_kb);
    addstr(buf);
    refresh();

    move(6, 0);
    addstr("Creating file...");
    refresh();

    FIL file;
    FRESULT fr = f_open(&file, filename, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK) {
        move(7, 0);
        snprintf(buf, sizeof(buf), "Error: Cannot create file (FRESULT=%d)", fr);
        addstr(buf);
        move(LINES - 3, 0);
        addstr("Press any key to return to menu...");
        refresh();
        timeout(-1);
        while (getch() == ERR);
        return;
    }

    // Write test pattern
    uint8_t buffer[512];
    uint32_t total_bytes = size_kb * 1024;
    uint32_t written = 0;

    move(7, 0);
    addstr("Progress: [                                                  ]");
    move(8, 0);
    addstr("  0%");
    refresh();

    while (written < total_bytes) {
        // Fill buffer with test pattern
        for (int i = 0; i < 512; i++) {
            buffer[i] = (written + i) & 0xFF;
        }

        UINT bw;
        fr = f_write(&file, buffer, 512, &bw);
        if (fr != FR_OK || bw != 512) {
            f_close(&file);
            move(9, 0);
            snprintf(buf, sizeof(buf), "Error: Write failed (FRESULT=%d)", fr);
            addstr(buf);
            move(LINES - 3, 0);
            addstr("Press any key to return to menu...");
            refresh();
            timeout(-1);
            while (getch() == ERR);
            return;
        }

        written += bw;

        // Update progress bar every 10%
        if ((written % (total_bytes / 10)) == 0 || written == total_bytes) {
            int percent = (written * 100) / total_bytes;
            int bars = (written * 50) / total_bytes;
            move(7, 11);
            for (int i = 0; i < bars; i++) {
                addch('=');
            }
            move(8, 0);
            char pct[16];
            snprintf(pct, sizeof(pct), "%3d%%", percent);
            addstr(pct);
            refresh();
        }
    }

    f_close(&file);

    move(9, 0);
    attron(A_REVERSE);
    addstr("✓ File created successfully!");
    standend();

    move(11, 0);
    snprintf(buf, sizeof(buf), "File: %s", filename);
    addstr(buf);
    move(12, 0);
    snprintf(buf, sizeof(buf), "Size: %lu bytes", (unsigned long)total_bytes);
    addstr(buf);

    move(LINES - 3, 0);
    addstr("Press any key to return to menu...");
    refresh();
    timeout(-1);
    while (getch() == ERR);
}

//==============================================================================
// Read/Write Benchmark
//==============================================================================

void menu_benchmark(void) {
    // EXACT pattern from help.c
    flushinp();
    timeout(-1);

    // Configure timer for 1 Hz (1 second period) - from spi_test.c
    // System clock: 50 MHz
    // Prescaler: 49 (divide by 50) → 1 MHz tick rate
    // Auto-reload: 999999 → 1,000,000 / 1,000,000 = 1 Hz
    timer_init_bench();
    timer_config_bench(49, 999999);

    // Enable Timer interrupt (IRQ[0])
    irq_setmask(~(1 << 0));

    // Reset performance counters
    bytes_transferred_this_second = 0;
    bytes_per_second = 0;
    timer_tick_flag = 0;

    timer_start_bench();

    clear();
    move(0, 0);
    attron(A_REVERSE);
    addstr("=== SD Card Benchmark ===");
    standend();
    refresh();

    if (!g_card_mounted) {
        move(2, 0);
        addstr("Error: SD card not mounted!");
        move(4, 0);
        addstr("Please detect and mount card first (Menu option 1).");
        move(LINES - 3, 0);
        addstr("Press any key to return to menu...");
        refresh();
        timeout(-1);
        while (getch() == ERR);
        return;
    }

    move(2, 0);
    addstr("This will create a temporary 1 MB test file to measure read/write speed.");
    refresh();

    const char *test_filename = "BENCH.TMP";
    const uint32_t test_size = 1024 * 1024;  // 1 MB
    const uint32_t block_size = 512;
    const uint32_t num_blocks = test_size / block_size;

    // Write benchmark
    move(5, 0);
    attron(A_REVERSE);
    addstr("Write Benchmark:");
    standend();
    move(6, 0);
    addstr("Creating test file...");
    refresh();

    FIL file;
    FRESULT fr = f_open(&file, test_filename, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK) {
        move(7, 0);
        char buf[64];
        snprintf(buf, sizeof(buf), "Error: Cannot create file (FRESULT=%d)", fr);
        addstr(buf);
        move(LINES - 3, 0);
        addstr("Press any key to return...");
        refresh();
        timeout(-1);
        while (getch() == ERR);
        return;
    }

    uint8_t buffer[512];
    for (int i = 0; i < 512; i++) {
        buffer[i] = i & 0xFF;
    }

    move(7, 0);
    addstr("Writing 1 MB...                    ");
    move(8, 0);
    addstr("Progress: [                                        ] 0%");
    move(10, 0);
    addstr("Speed: 0 B/s");
    refresh();

    uint32_t write_errors = 0;
    uint8_t last_tick_flag = 0;

    for (uint32_t i = 0; i < num_blocks; i++) {
        UINT bw;
        fr = f_write(&file, buffer, block_size, &bw);
        if (fr != FR_OK || bw != block_size) {
            write_errors++;
        } else {
            // Track bytes transferred for interrupt-based speed calculation
            bytes_transferred_this_second += block_size;
        }

        // Check if timer interrupt fired (every 1 second) - force display update
        if (timer_tick_flag != last_tick_flag) {
            last_tick_flag = timer_tick_flag;

            int percent = (i * 100) / num_blocks;
            int bars = (i * 48) / num_blocks;

            move(8, 0);
            addstr("Progress: [");
            for (int b = 0; b < bars; b++) {
                addch('=');
            }
            for (int b = bars; b < 48; b++) {
                addch(' ');
            }
            char pct[16];
            snprintf(pct, sizeof(pct), "] %3d%%", percent);
            addstr(pct);

            move(9, 0);
            char blk[64];
            snprintf(blk, sizeof(blk), "Blocks written: %lu / %lu",
                     (unsigned long)(i + 1), (unsigned long)num_blocks);
            addstr(blk);
            clrtoeol();

            move(10, 0);
            char speed_buf[32];
            format_bytes_per_sec(bytes_per_second, speed_buf, sizeof(speed_buf));
            snprintf(blk, sizeof(blk), "Speed: %s", speed_buf);
            addstr(blk);
            clrtoeol();

            refresh();
        }

        // Also update progress bar every 16 blocks (every ~8KB) for smoother updates
        if ((i & 0x0F) == 0 || i == num_blocks - 1) {
            int percent = (i * 100) / num_blocks;
            int bars = (i * 48) / num_blocks;  // 48 character wide bar

            move(8, 0);
            addstr("Progress: [");
            for (int b = 0; b < bars; b++) {
                addch('=');
            }
            for (int b = bars; b < 48; b++) {
                addch(' ');
            }
            char pct[16];
            snprintf(pct, sizeof(pct), "] %3d%%", percent);
            addstr(pct);

            // Show current block count
            move(9, 0);
            char blk[64];
            snprintf(blk, sizeof(blk), "Blocks written: %lu / %lu",
                     (unsigned long)(i + 1), (unsigned long)num_blocks);
            addstr(blk);
            clrtoeol();

            // Show real-time speed (updated by interrupt every second)
            move(10, 0);
            char speed_buf[32];
            format_bytes_per_sec(bytes_per_second, speed_buf, sizeof(speed_buf));
            snprintf(blk, sizeof(blk), "Speed: %s", speed_buf);
            addstr(blk);
            clrtoeol();

            refresh();
        }
    }

    f_close(&file);

    move(8, 0);
    if (write_errors == 0) {
        addstr("Progress: [================================================] 100%");
        move(11, 0);
        attron(A_REVERSE);
        addstr("✓ Write test completed successfully");
        standend();
        move(12, 0);
        char buf[64];
        char final_speed[32];
        format_bytes_per_sec(bytes_per_second, final_speed, sizeof(final_speed));
        snprintf(buf, sizeof(buf), "Final Speed: %s", final_speed);
        addstr(buf);
        move(13, 0);
        snprintf(buf, sizeof(buf), "Total: %lu bytes in %lu blocks",
                 (unsigned long)test_size, (unsigned long)num_blocks);
        addstr(buf);
    } else {
        char buf[64];
        snprintf(buf, sizeof(buf), "✗ Write errors: %lu", (unsigned long)write_errors);
        addstr(buf);
    }
    refresh();

    // Read benchmark
    move(15, 0);
    attron(A_REVERSE);
    addstr("Read Benchmark:");
    standend();
    move(16, 0);
    addstr("Reading test file...");
    refresh();

    fr = f_open(&file, test_filename, FA_READ);
    if (fr != FR_OK) {
        move(17, 0);
        char buf[64];
        snprintf(buf, sizeof(buf), "Error: Cannot open file (FRESULT=%d)", fr);
        addstr(buf);
        move(LINES - 3, 0);
        addstr("Press any key to return...");
        refresh();
        timeout(-1);
        while (getch() == ERR);
        return;
    }

    move(17, 0);
    addstr("Reading 1 MB...                    ");
    move(18, 0);
    addstr("Progress: [                                        ] 0%");
    move(20, 0);
    addstr("Speed: 0 B/s");
    refresh();

    // Reset byte counter for read test
    bytes_transferred_this_second = 0;
    last_tick_flag = timer_tick_flag;  // Reset flag tracker

    uint32_t read_errors = 0;
    for (uint32_t i = 0; i < num_blocks; i++) {
        UINT br;
        fr = f_read(&file, buffer, block_size, &br);
        if (fr != FR_OK || br != block_size) {
            read_errors++;
        } else {
            // Track bytes transferred for interrupt-based speed calculation
            bytes_transferred_this_second += block_size;
        }

        // Check if timer interrupt fired (every 1 second) - force display update
        if (timer_tick_flag != last_tick_flag) {
            last_tick_flag = timer_tick_flag;

            int percent = (i * 100) / num_blocks;
            int bars = (i * 48) / num_blocks;

            move(18, 0);
            addstr("Progress: [");
            for (int b = 0; b < bars; b++) {
                addch('=');
            }
            for (int b = bars; b < 48; b++) {
                addch(' ');
            }
            char pct[16];
            snprintf(pct, sizeof(pct), "] %3d%%", percent);
            addstr(pct);

            move(19, 0);
            char blk[64];
            snprintf(blk, sizeof(blk), "Blocks read: %lu / %lu",
                     (unsigned long)(i + 1), (unsigned long)num_blocks);
            addstr(blk);
            clrtoeol();

            move(20, 0);
            char speed_buf[32];
            format_bytes_per_sec(bytes_per_second, speed_buf, sizeof(speed_buf));
            snprintf(blk, sizeof(blk), "Speed: %s", speed_buf);
            addstr(blk);
            clrtoeol();

            refresh();
        }

        // Also update progress bar every 16 blocks (every ~8KB) for smoother updates
        if ((i & 0x0F) == 0 || i == num_blocks - 1) {
            int percent = (i * 100) / num_blocks;
            int bars = (i * 48) / num_blocks;  // 48 character wide bar

            move(18, 0);
            addstr("Progress: [");
            for (int b = 0; b < bars; b++) {
                addch('=');
            }
            for (int b = bars; b < 48; b++) {
                addch(' ');
            }
            char pct[16];
            snprintf(pct, sizeof(pct), "] %3d%%", percent);
            addstr(pct);

            // Show current block count
            move(19, 0);
            char blk[64];
            snprintf(blk, sizeof(blk), "Blocks read: %lu / %lu",
                     (unsigned long)(i + 1), (unsigned long)num_blocks);
            addstr(blk);
            clrtoeol();

            // Show real-time speed (updated by interrupt every second)
            move(20, 0);
            char speed_buf[32];
            format_bytes_per_sec(bytes_per_second, speed_buf, sizeof(speed_buf));
            snprintf(blk, sizeof(blk), "Speed: %s", speed_buf);
            addstr(blk);
            clrtoeol();

            refresh();
        }
    }

    f_close(&file);

    move(18, 0);
    if (read_errors == 0) {
        addstr("Progress: [================================================] 100%");
        move(21, 0);
        attron(A_REVERSE);
        addstr("✓ Read test completed successfully");
        standend();
        move(22, 0);
        char buf[64];
        char final_speed[32];
        format_bytes_per_sec(bytes_per_second, final_speed, sizeof(final_speed));
        snprintf(buf, sizeof(buf), "Final Speed: %s", final_speed);
        addstr(buf);
        move(23, 0);
        snprintf(buf, sizeof(buf), "Total: %lu bytes in %lu blocks",
                 (unsigned long)test_size, (unsigned long)num_blocks);
        addstr(buf);
    } else {
        char buf[64];
        snprintf(buf, sizeof(buf), "✗ Read errors: %lu", (unsigned long)read_errors);
        addstr(buf);
    }
    refresh();

    // Stop timer and disable interrupt
    timer_stop_bench();
    irq_setmask(~0);  // Disable all interrupts

    // Clean up test file
    move(25, 0);
    addstr("Deleting test file...");
    refresh();

    fr = f_unlink(test_filename);
    if (fr == FR_OK) {
        move(26, 0);
        addstr("✓ Test file deleted");
    } else {
        move(26, 0);
        addstr("Note: Could not delete test file (manual cleanup may be needed)");
    }

    move(LINES - 3, 0);
    addstr("Press any key to return...");
    refresh();
    timeout(-1);
    while (getch() == ERR);
}

//==============================================================================
// Main Menu
//==============================================================================

int main(void) {
    int selected_menu = MENU_DETECT_CARD;
    int old_selected = -1;
    int need_full_redraw = 1;

    // CRITICAL: Disable ALL interrupts for SD card manager
    // SD card SPI operations are NOT interrupt-safe and require precise timing
    uint32_t dummy;
    __asm__ volatile (".insn r 0x0B, 6, 3, %0, %1, x0" : "=r"(dummy) : "r"(~0));

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
                "Upload Overlay (UART)",
                "Browse & Run Overlays",
                "Upload & Execute (RAM)",
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

        // Handle input (EXACT pattern from spi_test.c)
        timeout(-1);
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
                    show_file_browser();
                    break;
                case MENU_UPLOAD_OVERLAY:
                    menu_upload_overlay();
                    break;
                case MENU_BROWSE_OVERLAYS:
                    menu_browse_overlays();
                    break;
                case MENU_UPLOAD_EXEC:
                    menu_upload_and_execute();
                    break;
                case MENU_CREATE_FILE:
                    menu_create_test_file();
                    break;
                case MENU_BENCHMARK:
                    menu_benchmark();
                    break;
                case MENU_SPI_SPEED:
                    menu_spi_speed();
                    break;
                case MENU_EJECT_CARD:
                    menu_eject_card();
                    break;
            }
            need_full_redraw = 1;  // EXACT pattern from spi_test.c - just redraw
        }
    }

    // Cleanup
    if (g_card_mounted) {
        f_mount(NULL, "", 0);
    }

    endwin();
    return 0;
}
