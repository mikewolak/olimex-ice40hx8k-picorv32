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
#include "diskio.h"
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
// Placed at fixed address 0x2A000 (via linker.ld .overlay_comm section) so overlays can find it
volatile void (*overlay_timer_irq_handler)(void) __attribute__((section(".overlay_comm"))) = 0;

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
#define MENU_PARTITION_INFO 3
#define MENU_FILE_BROWSER   4
#define MENU_UPLOAD_OVERLAY 5
#define MENU_UPLOAD_BOOTLOADER 6
#define MENU_UPLOAD_BOOTLOADER_COMPRESSED 7
#define MENU_BROWSE_OVERLAYS 8
#define MENU_UPLOAD_EXEC    9
#define MENU_CREATE_FILE    10
#define MENU_BENCHMARK      11
#define MENU_SPI_SPEED      12
#define MENU_EJECT_CARD     13
#define NUM_MENU_OPTIONS    14

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

        // Check if MBR exists with bootloader partition
        move(11, 0);
        addstr("Checking partition scheme...");
        refresh();

        int has_bootloader_partition = 0;
        const char *mount_path = "";  // Default: mount whole drive

        // Check for MBR signature
        if (test_block[510] == 0x55 && test_block[511] == 0xAA) {
            // Check first partition entry (offset 446)
            BYTE *part0 = &test_block[446];
            BYTE ptype = part0[4];
            uint32_t lba_start = part0[8] | (part0[9] << 8) | (part0[10] << 16) | (part0[11] << 24);
            uint32_t lba_size = part0[12] | (part0[13] << 8) | (part0[14] << 16) | (part0[15] << 24);

            // If partition 1 is bootloader (Type 0xDA, sectors 1-1024), mount partition 2
            if (ptype == 0xDA && lba_start == 1 && lba_size == 1024) {
                has_bootloader_partition = 1;
                mount_path = "0:2";  // Mount filesystem partition
                move(12, 0);
                addstr("✓ MBR with bootloader partition detected");
            } else {
                move(12, 0);
                addstr("✓ MBR detected (no bootloader partition)");
            }
        } else {
            move(12, 0);
            addstr("✓ Simple partition scheme (no MBR)");
        }

        move(13, 0);
        snprintf(buf, sizeof(buf), "Mounting: %s",
                 has_bootloader_partition ? "Partition 2 (filesystem)" : "Whole drive");
        addstr(buf);
        refresh();

        move(14, 0);
        addstr("Calling f_mount...");
        refresh();

        FRESULT fr = f_mount(&g_fs, mount_path, 1);
        move(15, 0);
        if (fr == FR_OK) {
            g_card_mounted = 1;
            addstr("✓ Filesystem mounted successfully");

            // Get volume label
            char label[24];
            DWORD vsn;
            fr = f_getlabel(mount_path, label, &vsn);
            if (fr == FR_OK && label[0]) {
                move(16, 0);
                snprintf(buf, sizeof(buf), "Volume Label: %s", label);
                addstr(buf);
            }

            // Get free space
            FATFS *fs;
            DWORD fre_clust;
            fr = f_getfree(mount_path, &fre_clust, &fs);
            if (fr == FR_OK) {
                move(17, 0);
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
// Partition Information
//==============================================================================

void menu_partition_info(void) {
    clear();
    move(0, 0);
    attron(A_REVERSE);
    addstr("=== Partition Information ===");
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

    char buf[80];
    int row = 2;

    // Read sector 0 to check for MBR or VBR
    BYTE mbr[512];
    int has_mbr = 0;
    const char *partition_scheme_name = "Unknown";

    if (disk_read(0, mbr, 0, 1) == RES_OK) {
        // Check if it's an MBR or VBR by looking at the partition table area
        // MBR has partition entries at 0x1BE (446), VBR has filesystem data there
        BYTE *part0 = &mbr[446];
        int looks_like_mbr = 0;

        // Check if first partition entry looks valid (bootable flag is 0x00 or 0x80)
        if ((part0[0] == 0x00 || part0[0] == 0x80) && part0[4] != 0x00) {
            // Check if partition type is a known value
            BYTE ptype = part0[4];
            if (ptype == 0x01 || ptype == 0x04 || ptype == 0x06 || ptype == 0x07 ||
                ptype == 0x0B || ptype == 0x0C || ptype == 0xDA || ptype == 0xEE) {
                looks_like_mbr = 1;
            }
        }

        if (looks_like_mbr) {
            has_mbr = 1;
            partition_scheme_name = "MBR (Master Boot Record)";
        } else {
            partition_scheme_name = "Simple (No Partition Table)";
        }
    }

    // Display partition scheme prominently at the top
    move(row++, 0);
    addstr("Partition Scheme: ");
    addstr(partition_scheme_name);
    row++;

    // Get and display card size
    LBA_t total_sectors = 0;
    if (disk_ioctl(0, GET_SECTOR_COUNT, &total_sectors) == RES_OK) {
        move(row++, 0);
        addstr("Card Capacity:");
        move(row++, 2);
        uint64_t total_mb = ((uint64_t)total_sectors * 512) / (1024 * 1024);
        snprintf(buf, sizeof(buf), "Total Size: %lu MB (%lu GB)",
                 (unsigned long)total_mb, (unsigned long)(total_mb / 1024));
        addstr(buf);
        move(row++, 2);
        snprintf(buf, sizeof(buf), "Total Sectors: %lu (512 bytes each)", (unsigned long)total_sectors);
        addstr(buf);
        row++;
    }

    // Now show detailed boot sector information
    if (disk_read(0, mbr, 0, 1) == RES_OK) {
        move(row++, 0);
        addstr("Boot Sector (Sector 0):");

        // Always show signature
        move(row++, 2);
        snprintf(buf, sizeof(buf), "Signature: 0x%02X%02X %s",
                 mbr[511], mbr[510],
                 (mbr[510] == 0x55 && mbr[511] == 0xAA) ? "(Valid)" : "(Invalid)");
        addstr(buf);

        if (has_mbr) {
            // Disk signature (offset 0x1B8)
            uint32_t disk_sig = mbr[0x1B8] | (mbr[0x1B9] << 8) | (mbr[0x1BA] << 16) | (mbr[0x1BB] << 24);
            move(row++, 2);
            snprintf(buf, sizeof(buf), "Disk Signature: 0x%08lX", (unsigned long)disk_sig);
            addstr(buf);

            row++;
            move(row++, 0);
            addstr("Partition Table:");

            // Parse partition entries
            for (int i = 0; i < 4; i++) {
                BYTE *part = &mbr[446 + (i * 16)];
                BYTE type = part[4];

                if (type != 0x00) {  // Non-empty partition
                    uint32_t lba_start = part[8] | (part[9] << 8) | (part[10] << 16) | (part[11] << 24);
                    uint32_t lba_size = part[12] | (part[13] << 8) | (part[14] << 16) | (part[15] << 24);
                    uint32_t size_mb = (uint32_t)(((uint64_t)lba_size * 512) / (1024 * 1024));

                    move(row++, 2);
                    snprintf(buf, sizeof(buf), "Partition %d:", i + 1);
                    addstr(buf);

                    move(row++, 4);
                    const char *type_str;
                    switch (type) {
                        case 0x01: type_str = "FAT12"; break;
                        case 0x04: type_str = "FAT16 (< 32 MB)"; break;
                        case 0x06: type_str = "FAT16"; break;
                        case 0x0B: type_str = "FAT32 (CHS)"; break;
                        case 0x0C: type_str = "FAT32 (LBA)"; break;
                        case 0x07: type_str = "exFAT/NTFS"; break;
                        case 0xDA: type_str = "Non-FS Data (Bootloader)"; break;
                        case 0xEE: type_str = "GPT Protective"; break;
                        default: type_str = "Unknown"; break;
                    }
                    snprintf(buf, sizeof(buf), "Type: 0x%02X (%s)", type, type_str);
                    addstr(buf);

                    move(row++, 4);
                    snprintf(buf, sizeof(buf), "Start Sector: %lu", (unsigned long)lba_start);
                    addstr(buf);

                    move(row++, 4);
                    snprintf(buf, sizeof(buf), "Size: %lu sectors (%lu MB)",
                             (unsigned long)lba_size, (unsigned long)size_mb);
                    addstr(buf);

                    move(row++, 4);
                    snprintf(buf, sizeof(buf), "Bootable: %s", (part[0] & 0x80) ? "Yes" : "No");
                    addstr(buf);
                    row++;
                }
            }
        } else {
            // It's a VBR (Volume Boot Record) - direct filesystem without partition table
            // OEM Name (offset 0x03, 8 bytes)
            move(row++, 2);
            char oem[9];
            memcpy(oem, &mbr[3], 8);
            oem[8] = '\0';
            snprintf(buf, sizeof(buf), "OEM Name: %.8s", oem);
            addstr(buf);

            // Check if it's exFAT (OEM name starts with "EXFAT")
            if (memcmp(oem, "EXFAT", 5) == 0) {
                // exFAT boot sector structure
                // Bytes per sector shift (offset 0x6C, 1 byte) - actual size = 2^shift
                uint8_t bytes_shift = mbr[0x6C];
                move(row++, 2);
                snprintf(buf, sizeof(buf), "Bytes/Sector: %u (2^%u)", 1 << bytes_shift, bytes_shift);
                addstr(buf);

                // Sectors per cluster shift (offset 0x6D, 1 byte)
                uint8_t cluster_shift = mbr[0x6D];
                move(row++, 2);
                snprintf(buf, sizeof(buf), "Sectors/Cluster: %u (2^%u)", 1 << cluster_shift, cluster_shift);
                addstr(buf);
            } else {
                // FAT12/16/32 boot sector structure
                // Bytes per sector (offset 0x0B, 2 bytes)
                uint16_t bytes_per_sec = mbr[0x0B] | (mbr[0x0C] << 8);
                move(row++, 2);
                snprintf(buf, sizeof(buf), "Bytes/Sector: %u", bytes_per_sec);
                addstr(buf);

                // Sectors per cluster (offset 0x0D, 1 byte)
                move(row++, 2);
                snprintf(buf, sizeof(buf), "Sectors/Cluster: %u", mbr[0x0D]);
                addstr(buf);
            }
            row++;
        }
    }

    // Get filesystem info if mounted
    if (g_card_mounted) {
        move(row++, 0);
        addstr("Filesystem Information:");

        FATFS *fs;
        DWORD fre_clust;
        FRESULT res = f_getfree("0:", &fre_clust, &fs);

        if (res == FR_OK) {
            // If we have an MBR, read the Volume Boot Record from the first partition
            if (has_mbr) {
                BYTE vbr[512];
                LBA_t vbr_sector = 0;

                // Find first FAT partition start sector
                for (int i = 0; i < 4; i++) {
                    BYTE *part = &mbr[446 + (i * 16)];
                    BYTE type = part[4];
                    if (type != 0x00 && type != 0xDA && type != 0xEE) {
                        vbr_sector = part[8] | (part[9] << 8) | (part[10] << 16) | (part[11] << 24);
                        break;
                    }
                }

                // Read and display VBR info
                if (vbr_sector > 0 && disk_read(0, vbr, vbr_sector, 1) == RES_OK) {
                    move(row++, 2);
                    addstr("Volume Boot Record (Partition 1):");

                    move(row++, 4);
                    snprintf(buf, sizeof(buf), "Boot Signature: 0x%02X%02X %s",
                             vbr[511], vbr[510],
                             (vbr[510] == 0x55 && vbr[511] == 0xAA) ? "(Valid)" : "(Invalid)");
                    addstr(buf);

                    // OEM Name (offset 0x03, 8 bytes)
                    move(row++, 4);
                    char oem[9];
                    memcpy(oem, &vbr[3], 8);
                    oem[8] = '\0';
                    snprintf(buf, sizeof(buf), "OEM Name: %.8s", oem);
                    addstr(buf);

                    // Bytes per sector (offset 0x0B, 2 bytes)
                    uint16_t bytes_per_sec = vbr[0x0B] | (vbr[0x0C] << 8);
                    move(row++, 4);
                    snprintf(buf, sizeof(buf), "Bytes/Sector: %u", bytes_per_sec);
                    addstr(buf);

                    // Sectors per cluster (offset 0x0D, 1 byte)
                    move(row++, 4);
                    snprintf(buf, sizeof(buf), "Sectors/Cluster: %u", vbr[0x0D]);
                    addstr(buf);

                    row++;
                }
            }

            // Total and free space
            DWORD tot_sect = (fs->n_fatent - 2) * fs->csize;
            DWORD fre_sect = fre_clust * fs->csize;
            uint32_t tot_mb = (uint32_t)(((uint64_t)tot_sect * 512) / (1024 * 1024));
            uint32_t fre_mb = (uint32_t)(((uint64_t)fre_sect * 512) / (1024 * 1024));
            uint32_t used_mb = tot_mb - fre_mb;

            move(row++, 2);
            const char *fs_type;
            switch (fs->fs_type) {
                case FS_FAT12: fs_type = "FAT12"; break;
                case FS_FAT16: fs_type = "FAT16"; break;
                case FS_FAT32: fs_type = "FAT32"; break;
                case FS_EXFAT: fs_type = "exFAT"; break;
                default: fs_type = "Unknown"; break;
            }
            snprintf(buf, sizeof(buf), "Type: %s", fs_type);
            addstr(buf);

            move(row++, 2);
            snprintf(buf, sizeof(buf), "Total Space: %lu MB", (unsigned long)tot_mb);
            addstr(buf);

            move(row++, 2);
            snprintf(buf, sizeof(buf), "Used Space: %lu MB", (unsigned long)used_mb);
            addstr(buf);

            move(row++, 2);
            snprintf(buf, sizeof(buf), "Free Space: %lu MB", (unsigned long)fre_mb);
            addstr(buf);

            move(row++, 2);
            uint32_t usage_pct = tot_mb > 0 ? (used_mb * 100) / tot_mb : 0;
            snprintf(buf, sizeof(buf), "Usage: %lu%%", (unsigned long)usage_pct);
            addstr(buf);

            // Draw usage bar
            move(row++, 2);
            addstr("Usage Bar: [");
            int bar_width = 40;
            int filled = (usage_pct * bar_width) / 100;
            for (int i = 0; i < bar_width; i++) {
                if (i < filled) {
                    addch('#');
                } else {
                    addch('-');
                }
            }
            addch(']');
        } else {
            move(row++, 2);
            snprintf(buf, sizeof(buf), "Error reading filesystem info (code: %d)", res);
            addstr(buf);
        }
    } else {
        move(row++, 0);
        addstr("Filesystem: Not mounted");
    }

    refresh();
    move(LINES - 3, 0);
    addstr("Press any key to return...");
    refresh();

    timeout(-1);
    while (getch() == ERR);
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
        "MBR with bootloader partition (512KB + FS)",
        "GPT partition table (exFAT only)"
    };

    int selected_fs = 2;  // Default: exFAT
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

            for (int i = 0; i < 4; i++) {
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
            // Confirmation mode - wait for real key (matches delete_file pattern)
            while (1) {
                flushinp();
                timeout(-1);
                ch = getch();
                if (ch != ERR) break;  // Got a real key
            }
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
                } else if (current_menu == 1 && selected_part < 3) {  // Changed to 3 for 4 options (0-3)
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
    } else if (selected_part == 3) {
        fmt_opt.fmt |= 0x08;  // GPT (moved to index 3, was incorrectly at 2)
    }
    // Note: selected_part == 1 (MBR) and selected_part == 2 (MBR+bootloader) handled below

    // Work area for f_mkfs (needs to be large enough)
    static BYTE work[4096];

    // Special handling for bootloader partition scheme (selected_part == 2)
    FRESULT fr = FR_OK;
    if (selected_part == 2) {
        // Use f_fdisk() to create MBR with bootloader partition + FS partition
        move(5, 0);
        addstr("Creating MBR with bootloader partition using f_fdisk()...");
        refresh();

        // Get total sector count
        LBA_t total_sectors = 0;
        if (disk_ioctl(0, GET_SECTOR_COUNT, &total_sectors) != RES_OK) {
            move(7, 0);
            addstr("ERROR: Failed to get SD card size");
            refresh();
            goto format_error;
        }

        // Partition layout for f_fdisk:
        // Partition 1: Bootloader (512KB = 1024 sectors)
        // Partition 2: Filesystem (remaining space)
        //
        // f_fdisk() partition table:
        // - Array terminated by 0
        // - Values are sector counts
        // - f_fdisk will automatically place partitions starting from sector 1

        #define BOOT_PART_SECTORS 1024

        LBA_t ptbl[4];  // Partition table for f_fdisk (max 4 partitions)
        ptbl[0] = BOOT_PART_SECTORS;  // Partition 1: 1024 sectors (512KB) for bootloader
        ptbl[1] = total_sectors - BOOT_PART_SECTORS - 63;  // Partition 2: Remaining space for filesystem
        ptbl[2] = 0;  // Terminator (no more partitions)
        ptbl[3] = 0;

        move(6, 0);
        addstr("========================================");
        move(7, 0);
        addstr("STEP 1: Creating MBR with f_fdisk()");
        move(8, 0);
        addstr("========================================");
        move(9, 0);
        snprintf(buf, sizeof(buf), "  Partition 1: %u sectors (bootloader)", BOOT_PART_SECTORS);
        addstr(buf);
        move(10, 0);
        snprintf(buf, sizeof(buf), "  Partition 2: %lu sectors (filesystem)",
                 (unsigned long)ptbl[1]);
        addstr(buf);
        move(11, 0);
        addstr("  Calling f_fdisk()...");
        refresh();

        // f_fdisk creates MBR with proper partition table
        // Physical drive 0, partition table, work buffer
        fr = f_fdisk(0, ptbl, work);

        if (fr != FR_OK) {
            move(12, 0);
            snprintf(buf, sizeof(buf), "✗ ERROR: f_fdisk failed with code %d (%s)", fr, fresult_to_string(fr));
            addstr(buf);
            refresh();
            goto format_error;
        }

        move(12, 0);
        addstr("  ✓ MBR created successfully with f_fdisk()");
        refresh();

        // Verify MBR was created correctly
        move(13, 0);
        addstr("  Verifying MBR...");
        refresh();

        BYTE verify_buf[512];
        if (disk_read(0, verify_buf, 0, 1) != RES_OK) {
            move(14, 0);
            addstr("✗ ERROR: Failed to read back sector 0");
            refresh();
            goto format_error;
        }

        // Check MBR signature
        if (verify_buf[510] != 0x55 || verify_buf[511] != 0xAA) {
            move(14, 0);
            snprintf(buf, sizeof(buf), "✗ ERROR: MBR signature invalid! Got 0x%02X%02X",
                     verify_buf[511], verify_buf[510]);
            addstr(buf);
            refresh();
            goto format_error;
        }

        // Display partition info
        BYTE *verify_part1 = &verify_buf[446];
        BYTE *verify_part2 = &verify_buf[462];

        uint32_t part1_start = verify_part1[8] | (verify_part1[9] << 8) |
                               (verify_part1[10] << 16) | (verify_part1[11] << 24);
        uint32_t part1_size = verify_part1[12] | (verify_part1[13] << 8) |
                              (verify_part1[14] << 16) | (verify_part1[15] << 24);
        uint32_t part2_start = verify_part2[8] | (verify_part2[9] << 8) |
                               (verify_part2[10] << 16) | (verify_part2[11] << 24);
        uint32_t part2_size = verify_part2[12] | (verify_part2[13] << 8) |
                              (verify_part2[14] << 16) | (verify_part2[15] << 24);

        move(14, 0);
        addstr("  ✓ MBR verified - signature correct");
        move(15, 0);
        snprintf(buf, sizeof(buf), "  Partition 1: Type 0x%02X, Start %lu, Size %lu sectors",
                 verify_part1[4], (unsigned long)part1_start, (unsigned long)part1_size);
        addstr(buf);
        move(16, 0);
        snprintf(buf, sizeof(buf), "  Partition 2: Type 0x%02X, Start %lu, Size %lu sectors",
                 verify_part2[4], (unsigned long)part2_start, (unsigned long)part2_size);
        addstr(buf);
        refresh();

        // Now we need to change partition 1 type to 0xDA (bootloader)
        // f_fdisk() creates all partitions as type 0x07, we need to customize
        move(17, 0);
        addstr("  Updating partition 1 type to 0xDA (bootloader)...");
        refresh();

        verify_buf[446 + 4] = 0xDA;  // Set partition 1 type to Non-FS data

        if (disk_write(0, verify_buf, 0, 1) != RES_OK) {
            move(18, 0);
            addstr("✗ ERROR: Failed to update partition type");
            refresh();
            goto format_error;
        }

        move(18, 0);
        addstr("  ✓ Partition 1 type updated to 0xDA");
        refresh();

        // Now format partition 2 (filesystem partition)
        move(20, 0);
        addstr("========================================");
        move(21, 0);
        addstr("STEP 2: Formatting Partition 2 Filesystem");
        move(22, 0);
        addstr("========================================");
        move(23, 0);
        snprintf(buf, sizeof(buf), "  Format type: %s", fs_types[selected_fs]);
        addstr(buf);
        move(24, 0);
        snprintf(buf, sizeof(buf), "  Partition start: Sector %lu", (unsigned long)part2_start);
        addstr(buf);
        move(25, 0);
        snprintf(buf, sizeof(buf), "  Partition size: %lu sectors (%.1f MB)",
                 (unsigned long)part2_size,
                 (float)(part2_size / 2048.0));
        addstr(buf);
        move(26, 0);
        addstr("  Calling f_mkfs(\"0:2\", ...)...");
        refresh();

        // Format partition 2 by specifying "0:2" (drive 0, partition 2)
        // FatFS will read the MBR we created with f_fdisk() and format ONLY partition 2
        fr = f_mkfs("0:2", &fmt_opt, work, sizeof(work));

        if (fr != FR_OK) {
            move(27, 0);
            snprintf(buf, sizeof(buf), "✗ ERROR: f_mkfs failed with code %d (%s)", fr, fresult_to_string(fr));
            addstr(buf);
            refresh();
            goto format_error;
        }

        move(27, 0);
        addstr("  ✓ Filesystem formatted successfully");
        refresh();

        // POST-FORMAT VALIDATION
        move(29, 0);
        addstr("========================================");
        move(30, 0);
        addstr("STEP 3: Post-Format Validation");
        move(31, 0);
        addstr("========================================");
        move(32, 0);
        addstr("  Re-reading sector 0 (MBR)...");
        refresh();

        if (disk_read(0, verify_buf, 0, 1) != RES_OK) {
            move(33, 0);
            addstr("✗ ERROR: Cannot read sector 0 after format");
            refresh();
            goto format_error;
        }

        // Validate MBR still intact
        if (verify_buf[510] != 0x55 || verify_buf[511] != 0xAA) {
            move(33, 0);
            attron(A_REVERSE);
            addstr("✗✗✗ CRITICAL: MBR WAS OVERWRITTEN! ✗✗✗");
            standend();
            move(34, 0);
            snprintf(buf, sizeof(buf), "  Sector 0 signature: 0x%02X%02X (expected 0xAA55)",
                     verify_buf[511], verify_buf[510]);
            addstr(buf);
            refresh();
            goto format_error;
        }

        verify_part1 = &verify_buf[446];
        verify_part2 = &verify_buf[462];

        uint32_t verify_part1_start = verify_part1[8] | (verify_part1[9] << 8) |
                                      (verify_part1[10] << 16) | (verify_part1[11] << 24);
        uint32_t verify_part1_size = verify_part1[12] | (verify_part1[13] << 8) |
                                     (verify_part1[14] << 16) | (verify_part1[15] << 24);
        uint32_t verify_part2_start = verify_part2[8] | (verify_part2[9] << 8) |
                                      (verify_part2[10] << 16) | (verify_part2[11] << 24);
        uint32_t verify_part2_size = verify_part2[12] | (verify_part2[13] << 8) |
                                     (verify_part2[14] << 16) | (verify_part2[15] << 24);

        move(33, 0);
        addstr("  ✓ MBR signature intact (0xAA55)");
        move(34, 0);
        snprintf(buf, sizeof(buf), "  Partition 1: Type 0x%02X, Start %lu, Size %lu sectors",
                 verify_part1[4], (unsigned long)verify_part1_start, (unsigned long)verify_part1_size);
        addstr(buf);
        move(35, 0);
        snprintf(buf, sizeof(buf), "  Partition 2: Type 0x%02X, Start %lu, Size %lu sectors",
                 verify_part2[4], (unsigned long)verify_part2_start, (unsigned long)verify_part2_size);
        addstr(buf);

        // Verify partition 1 is bootloader type
        if (verify_part1[4] != 0xDA) {
            move(37, 0);
            attron(A_REVERSE);
            snprintf(buf, sizeof(buf), "✗ WARNING: Partition 1 type is 0x%02X (expected 0xDA)", verify_part1[4]);
            addstr(buf);
            standend();
            refresh();
        } else {
            move(37, 0);
            addstr("  ✓ Partition 1 type correct (0xDA - bootloader)");
        }

        move(39, 0);
        attron(A_REVERSE);
        addstr("✓✓✓ SUCCESS! MBR + Filesystem Created and Verified ✓✓✓");
        standend();
        refresh();

        #undef BOOT_PART_SECTORS

    } else {
        // Standard formatting (no bootloader partition)

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
    fr = f_mkfs("", &fmt_opt, work, sizeof(work));

    } // End else block for standard formatting

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
        // When MBR with bootloader partition was created, mount partition 2 (0:2)
        // Otherwise mount whole drive
        if (selected_part == 2) {
            fr = f_mount(&g_fs, "0:2", 1);  // Mount filesystem partition (starts at sector 1025)
        } else {
            fr = f_mount(&g_fs, "", 1);     // Mount whole drive
        }
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

format_error:  // Jump target for bootloader partition format errors
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
// Upload Bootloader to Raw Partition
//==============================================================================

void menu_upload_bootloader(void) {
    clear();
    move(0, 0);
    attron(A_REVERSE);
    addstr("=== Upload Bootloader to Raw Partition ===");
    standend();

    move(2, 0);

    // Check if card is detected
    if (!g_card_detected) {
        addstr("Error: No SD card detected!");
        move(4, 0);
        addstr("Please detect card first (Menu option 1).");
        move(LINES - 3, 0);
        addstr("Press any key to return to menu...");
        refresh();

        timeout(-1);
        while (getch() == ERR);
        return;
    }

    // Check if bootloader partition exists by reading MBR
    addstr("Checking for bootloader partition...");
    refresh();

    BYTE mbr[512];
    DRESULT disk_res = disk_read(0, mbr, 0, 1);

    if (disk_res != RES_OK) {
        move(4, 0);
        addstr("Error: Cannot read MBR from card!");
        move(5, 0);
        char errstr[32];
        snprintf(errstr, sizeof(errstr), "(Disk error: %d)", disk_res);
        addstr(errstr);
        move(LINES - 3, 0);
        addstr("Press any key to return to menu...");
        refresh();

        timeout(-1);
        while (getch() == ERR);
        return;
    }

    // Check if MBR has valid signature
    if (mbr[510] != 0x55 || mbr[511] != 0xAA) {
        move(4, 0);
        addstr("Error: Invalid MBR signature!");
        move(5, 0);
        addstr("Card does not have a valid Master Boot Record.");
        move(6, 0);
        addstr("Please format card with 'MBR with bootloader' option first.");
        move(LINES - 3, 0);
        addstr("Press any key to return to menu...");
        refresh();

        timeout(-1);
        while (getch() == ERR);
        return;
    }

    // Check first partition entry (should be type 0xDA at sector 1)
    BYTE *part0 = &mbr[446];
    BYTE ptype = part0[4];
    uint32_t lba_start = part0[8] | (part0[9] << 8) | (part0[10] << 16) | (part0[11] << 24);
    uint32_t lba_size = part0[12] | (part0[13] << 8) | (part0[14] << 16) | (part0[15] << 24);

    if (ptype != 0xDA || lba_start != 63 || lba_size != 1024) {
        move(4, 0);
        addstr("Error: Bootloader partition not found!");
        move(5, 0);
        addstr("Expected: Type 0xDA, Sectors 63-1086 (512KB)");
        move(6, 0);
        char buf[64];
        snprintf(buf, sizeof(buf), "Found: Type 0x%02X, Start %lu, Size %lu",
                 ptype, (unsigned long)lba_start, (unsigned long)lba_size);
        addstr(buf);
        move(7, 0);
        addstr("Please format card with 'MBR with bootloader' option first.");
        move(LINES - 3, 0);
        addstr("Press any key to return to menu...");
        refresh();

        timeout(-1);
        while (getch() == ERR);
        return;
    }

    // Bootloader partition verified!
    move(4, 0);
    addstr("✓ Bootloader partition found:");
    move(5, 2);
    addstr("Type: 0xDA (Non-FS Data)");
    move(6, 2);
    addstr("Location: Sectors 1-1024");
    move(7, 2);
    addstr("Size: 512 KB");

    move(9, 0);
    addstr("This will upload bootloader code directly to raw sectors.");
    move(10, 0);
    addstr("Protocol: FAST streaming (use fw_upload_fast tool)");
    move(11, 0);
    addstr("Maximum size: 512 KB");

    move(13, 0);
    attron(A_REVERSE);
    addstr("WARNING: Data integrity is critical for bootloader!");
    standend();
    move(14, 0);
    addstr("CRC32 verification will be performed after upload.");

    move(16, 0);
    addstr("Ready to receive bootloader...");
    move(17, 0);
    addstr("Start upload from PC now using:");
    move(18, 0);
    addstr("  fw_upload_fast -p /dev/ttyUSB0 bootloader.bin");

    move(20, 0);
    refresh();

    // Exit ncurses temporarily for upload (direct UART access)
    endwin();

    // Call bootloader upload function
    FRESULT fr = bootloader_upload_to_partition();

    // Restore ncurses
    refresh();

    // Show result (keep debug output visible, don't clear)
    // clear();  // COMMENTED OUT - preserve debug output from upload
    move(0, 0);
    attron(A_REVERSE);
    addstr("=== Upload Result ===");
    standend();

    move(2, 0);
    if (fr == FR_OK) {
        attron(A_REVERSE);
        addstr("✓✓✓ SUCCESS! Bootloader uploaded and verified.");
        standend();

        move(4, 0);
        addstr("Bootloader written to sectors 1-1024");
        move(5, 0);
        addstr("CRC32 verification: PASSED");
        move(6, 0);
        addstr("Data integrity: 100% confirmed");
    } else {
        attron(A_REVERSE);
        addstr("✗✗✗ FAILED! Bootloader upload error.");
        standend();

        move(4, 0);
        addstr("Error: ");
        addstr(fresult_to_string(fr));

        move(5, 0);
        char errstr[32];
        snprintf(errstr, sizeof(errstr), "(Error code: %d)", fr);
        addstr(errstr);

        move(7, 0);
        attron(A_REVERSE);
        addstr("DO NOT ATTEMPT TO USE THIS BOOTLOADER!");
        standend();
        move(8, 0);
        addstr("Please retry the upload.");
    }

    move(LINES - 3, 0);
    addstr("Press any key to return to menu...");
    refresh();

    timeout(-1);
    while (getch() == ERR);
}

//==============================================================================
// Upload Compressed Bootloader (GZIP)
//==============================================================================

void menu_upload_bootloader_compressed(void) {
    clear();
    move(0, 0);
    attron(A_REVERSE);
    addstr("=== Upload COMPRESSED Bootloader (GZIP) ===");
    standend();

    move(2, 0);

    // Check if card is detected
    if (!g_card_detected) {
        addstr("Error: No SD card detected!");
        move(4, 0);
        addstr("Please detect card first (Menu option 1).");
        move(LINES - 3, 0);
        addstr("Press any key to return to menu...");
        refresh();

        timeout(-1);
        while (getch() == ERR);
        return;
    }

    // Check if bootloader partition exists by reading MBR
    addstr("Checking for bootloader partition...");
    refresh();

    BYTE mbr[512];
    DRESULT disk_res = disk_read(0, mbr, 0, 1);

    if (disk_res != RES_OK) {
        move(4, 0);
        addstr("Error: Cannot read MBR from card!");
        move(5, 0);
        char errstr[32];
        snprintf(errstr, sizeof(errstr), "(Disk error: %d)", disk_res);
        addstr(errstr);
        move(LINES - 3, 0);
        addstr("Press any key to return to menu...");
        refresh();

        timeout(-1);
        while (getch() == ERR);
        return;
    }

    // Check if MBR has valid signature
    if (mbr[510] != 0x55 || mbr[511] != 0xAA) {
        move(4, 0);
        addstr("Error: Invalid MBR signature!");
        move(5, 0);
        addstr("Card does not have a valid Master Boot Record.");
        move(6, 0);
        addstr("Please format card with 'MBR with bootloader' option first.");
        move(LINES - 3, 0);
        addstr("Press any key to return to menu...");
        refresh();

        timeout(-1);
        while (getch() == ERR);
        return;
    }

    // Check first partition entry (should be type 0xDA at sector 1)
    BYTE *part0 = &mbr[446];
    BYTE ptype = part0[4];
    uint32_t lba_start = part0[8] | (part0[9] << 8) | (part0[10] << 16) | (part0[11] << 24);
    uint32_t lba_size = part0[12] | (part0[13] << 8) | (part0[14] << 16) | (part0[15] << 24);

    if (ptype != 0xDA || lba_start != 63 || lba_size != 1024) {
        move(4, 0);
        addstr("Error: Bootloader partition not found!");
        move(5, 0);
        addstr("Expected: Type 0xDA, Sectors 63-1086 (512KB)");
        move(6, 0);
        char buf[64];
        snprintf(buf, sizeof(buf), "Found: Type 0x%02X, Start %lu, Size %lu",
                 ptype, (unsigned long)lba_start, (unsigned long)lba_size);
        addstr(buf);
        move(7, 0);
        addstr("Please format card with 'MBR with bootloader' option first.");
        move(LINES - 3, 0);
        addstr("Press any key to return to menu...");
        refresh();

        timeout(-1);
        while (getch() == ERR);
        return;
    }

    // Bootloader partition verified!
    move(4, 0);
    addstr("✓ Bootloader partition found:");
    move(5, 2);
    addstr("Type: 0xDA (Non-FS Data)");
    move(6, 2);
    addstr("Location: Sectors 1-1024");
    move(7, 2);
    addstr("Size: 512 KB");

    move(9, 0);
    addstr("This will upload GZIP-COMPRESSED bootloader and decompress");
    move(10, 0);
    addstr("it directly to raw sectors (supports up to 512KB uncompressed).");
    move(11, 0);
    addstr("Protocol: FAST streaming (use fw_upload_fast tool)");
    move(12, 0);
    addstr("Maximum compressed size: 96 KB");

    move(14, 0);
    attron(A_REVERSE);
    addstr("IMPORTANT: Upload the .bin.gz file, NOT the .bin file!");
    standend();
    move(15, 0);
    addstr("The firmware will decompress it automatically.");

    move(17, 0);
    addstr("Ready to receive compressed bootloader...");
    move(18, 0);
    addstr("Start upload from PC now using:");
    move(19, 0);
    addstr("  fw_upload_fast -p /dev/ttyUSB0 bootloader.bin.gz");

    move(21, 0);
    refresh();

    // Exit ncurses temporarily for upload (direct UART access)
    endwin();

    // Call compressed bootloader upload function
    FRESULT fr = bootloader_upload_compressed_to_partition();

    // Restore ncurses
    refresh();

    // Show result (keep debug output visible, don't clear)
    // clear();  // COMMENTED OUT - preserve debug output from upload
    move(0, 0);
    attron(A_REVERSE);
    addstr("=== Upload Result ===");
    standend();

    move(2, 0);
    if (fr == FR_OK) {
        attron(A_REVERSE);
        addstr("✓✓✓ SUCCESS! Compressed bootloader decompressed and installed.");
        standend();

        move(4, 0);
        addstr("Bootloader written to sectors 1-1024");
        move(5, 0);
        addstr("Compressed CRC32 verification: PASSED");
        move(6, 0);
        addstr("Decompression: SUCCESSFUL");
    } else {
        attron(A_REVERSE);
        addstr("✗✗✗ FAILED! Compressed bootloader upload error.");
        standend();

        move(4, 0);
        addstr("Error: ");
        addstr(fresult_to_string(fr));

        move(5, 0);
        char errstr[32];
        snprintf(errstr, sizeof(errstr), "(Error code: %d)", fr);
        addstr(errstr);

        move(7, 0);
        attron(A_REVERSE);
        addstr("DO NOT ATTEMPT TO USE THIS BOOTLOADER!");
        standend();
        move(8, 0);
        addstr("Please retry the upload.");
    }

    move(LINES - 3, 0);
    addstr("Press any key to return to menu...");
    refresh();

    timeout(-1);
    while (getch() == ERR);
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
    addstr("Enter overlay filename (e.g., mandelbrot_float.bin): ");
    refresh();

    // Get filename from user (support long filenames)
    char filename[256];
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

            printf("\r\n");
            printf("========================================\r\n");
            printf("Loading overlay: %s\r\n", info->filename);
            printf("========================================\r\n");

            // Load overlay to execution address
            overlay_info_t loaded_info;
            fr = overlay_load(info->filename, OVERLAY_EXEC_BASE, &loaded_info);

            if (fr != FR_OK) {
                printf("\r\nError: Failed to load overlay (error %d)\r\n", fr);
                printf("Press any key to return to menu...\r\n");
                getch();
            } else {
                // Execute overlay
                overlay_execute(loaded_info.entry_point);

                // Overlay returned
                printf("\r\nPress any key to return to menu...\r\n");
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
                "Format Card",
                "Partition Information",
                "File Browser",
                "Upload Overlay (UART)",
                "Upload Bootloader (UART)",
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
                case MENU_PARTITION_INFO:
                    menu_partition_info();
                    break;
                case MENU_FILE_BROWSER:
                    show_file_browser();
                    break;
                case MENU_UPLOAD_OVERLAY:
                    menu_upload_overlay();
                    break;
                case MENU_UPLOAD_BOOTLOADER:
                    menu_upload_bootloader();
                    break;
                case MENU_UPLOAD_BOOTLOADER_COMPRESSED:
                    menu_upload_bootloader_compressed();
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
