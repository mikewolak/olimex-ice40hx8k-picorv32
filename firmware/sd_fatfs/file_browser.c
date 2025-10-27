//==============================================================================
// Olimex iCE40HX8K-EVB RISC-V Platform
// File Browser - Implementation
//
// Beautiful scrollable file browser with full file management:
// - Scrollable file list with attributes (size, date, time)
// - Directory navigation
// - Create/delete directories
// - Delete files with confirmation
// - Sort by name or time
// - CRC32 checksum calculation
// - Total directory size display
//
// Copyright (c) October 2025 Michael Wolak
// Email: mikewolak@gmail.com, mike@epromfoundry.com
//==============================================================================

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "../../lib/incurses/curses.h"
#include "ff.h"
#include "file_browser.h"
#include "hardware.h"

//==============================================================================
// External Global (from sd_card_manager.c)
//==============================================================================

extern uint8_t g_card_mounted;

//==============================================================================
// Arrow Key Helper - Detects ESC [ A/B sequences from arrow keys
//==============================================================================

static int get_key_with_arrows(void) {
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
    return ch;
}

//==============================================================================
// CRC32 Functions (adapted from overlay_upload.c)
//==============================================================================

static uint32_t crc32_table[256];
static uint8_t crc32_initialized = 0;

static void crc32_init(void) {
    if (crc32_initialized) return;

    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc = crc >> 1;
            }
        }
        crc32_table[i] = crc;
    }
    crc32_initialized = 1;
}

static uint32_t calculate_file_crc32(const char *filename) {
    FIL file;
    FRESULT fr = f_open(&file, filename, FA_READ);
    if (fr != FR_OK) {
        return 0;
    }

    uint32_t crc = 0xFFFFFFFF;
    uint8_t buffer[512];
    UINT br;

    while (1) {
        fr = f_read(&file, buffer, sizeof(buffer), &br);
        if (fr != FR_OK || br == 0) break;

        for (UINT i = 0; i < br; i++) {
            crc = (crc >> 8) ^ crc32_table[(crc ^ buffer[i]) & 0xFF];
        }
    }

    f_close(&file);
    return ~crc;
}

//==============================================================================
// File List Management
//==============================================================================

#define MAX_FILES 64  // Reduced to fit long filenames in BSS

static FileEntry file_list[MAX_FILES];
static int num_files = 0;
static int sort_by_time = 0;  // 0 = sort by name, 1 = sort by time
static char current_path[256] = "/";

// Compare functions for sorting
static int compare_by_name(const void *a, const void *b) {
    const FileEntry *fa = (const FileEntry *)a;
    const FileEntry *fb = (const FileEntry *)b;

    // Directories first
    if (fa->is_dir && !fb->is_dir) return -1;
    if (!fa->is_dir && fb->is_dir) return 1;

    // Then by name
    return strcmp(fa->name, fb->name);
}

static int compare_by_time(const void *a, const void *b) {
    const FileEntry *fa = (const FileEntry *)a;
    const FileEntry *fb = (const FileEntry *)b;

    // Directories first
    if (fa->is_dir && !fb->is_dir) return -1;
    if (!fa->is_dir && fb->is_dir) return 1;

    // Then by time (oldest first, newest last)
    if (fa->date != fb->date) {
        return (fa->date < fb->date) ? -1 : 1;
    }
    return (fa->time < fb->time) ? -1 : 1;
}

// Simplified qsort implementation for embedded systems
static void swap_entries(FileEntry *a, FileEntry *b) {
    FileEntry temp = *a;
    *a = *b;
    *b = temp;
}

static void quicksort(FileEntry *arr, int low, int high, int (*cmp)(const void*, const void*)) {
    if (low < high) {
        FileEntry pivot = arr[high];
        int i = low - 1;

        for (int j = low; j < high; j++) {
            if (cmp(&arr[j], &pivot) < 0) {
                i++;
                swap_entries(&arr[i], &arr[j]);
            }
        }
        swap_entries(&arr[i + 1], &arr[high]);
        int pi = i + 1;

        quicksort(arr, low, pi - 1, cmp);
        quicksort(arr, pi + 1, high, cmp);
    }
}

static void sort_file_list(void) {
    if (num_files > 1) {
        if (sort_by_time) {
            quicksort(file_list, 0, num_files - 1, compare_by_time);
        } else {
            quicksort(file_list, 0, num_files - 1, compare_by_name);
        }
    }
}

//==============================================================================
// Directory Scanning
//==============================================================================

static int scan_directory(const char *path) {
    DIR dir;
    FILINFO fno;
    FRESULT fr;

    num_files = 0;

    fr = f_opendir(&dir, path);
    if (fr != FR_OK) {
        return -1;
    }

    // Add ".." entry if not at root
    if (strcmp(path, "/") != 0) {
        FileEntry *entry = &file_list[num_files];
        strcpy(entry->name, "..");
        entry->size = 0;
        entry->date = 0;
        entry->time = 0;
        entry->attrib = AM_DIR;
        entry->is_dir = 1;
        num_files++;
    }

    while (num_files < MAX_FILES) {
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK || fno.fname[0] == 0) break;

        // Skip "." entry (current dir - not useful in list)
        if (strcmp(fno.fname, ".") == 0) continue;

        // Store file info
        FileEntry *entry = &file_list[num_files];
        strncpy(entry->name, fno.fname, sizeof(entry->name) - 1);
        entry->name[sizeof(entry->name) - 1] = '\0';
        entry->size = (uint32_t)fno.fsize;
        entry->date = fno.fdate;
        entry->time = fno.ftime;
        entry->attrib = fno.fattrib;
        entry->is_dir = (fno.fattrib & AM_DIR) ? 1 : 0;

        num_files++;
    }

    f_closedir(&dir);

    // Sort the list
    sort_file_list();

    return num_files;
}

//==============================================================================
// Helper Functions
//==============================================================================

// Format file size with units
static void format_size(uint32_t size, char *buf, int buf_size) {
    if (size >= 1048576) {  // >= 1 MB
        uint32_t mb = size / 1048576;
        uint32_t kb = (size % 1048576) / 1024;
        snprintf(buf, buf_size, "%lu.%02lu MB", (unsigned long)mb, (unsigned long)kb / 10);
    } else if (size >= 1024) {  // >= 1 KB
        uint32_t kb = size / 1024;
        uint32_t b = size % 1024;
        snprintf(buf, buf_size, "%lu.%02lu KB", (unsigned long)kb, (unsigned long)b / 10);
    } else {
        snprintf(buf, buf_size, "%lu B", (unsigned long)size);
    }
}

// Decode FAT date/time
static void format_datetime(uint16_t date, uint16_t time, char *buf, int buf_size) {
    int year = ((date >> 9) & 0x7F) + 1980;
    int month = (date >> 5) & 0x0F;
    int day = date & 0x1F;
    int hour = (time >> 11) & 0x1F;
    int min = (time >> 5) & 0x3F;
    int sec = (time & 0x1F) * 2;

    snprintf(buf, buf_size, "%04d-%02d-%02d %02d:%02d:%02d",
             year, month, day, hour, min, sec);
}

// Calculate total directory size
static uint32_t calculate_dir_size(void) {
    uint32_t total = 0;
    for (int i = 0; i < num_files; i++) {
        if (!file_list[i].is_dir) {
            total += file_list[i].size;
        }
    }
    return total;
}

//==============================================================================
// Display Functions
//==============================================================================

static void draw_header(void) {
    move(0, 0);
    attron(A_REVERSE);
    addstr("=== FILE BROWSER ===");
    for (int i = 20; i < COLS; i++) addch(' ');
    standend();

    move(1, 0);
    char buf[128];
    snprintf(buf, sizeof(buf), "Path: %s", current_path);
    addstr(buf);
    clrtoeol();

    move(2, 0);
    uint32_t dir_size = calculate_dir_size();
    char size_buf[32];
    format_size(dir_size, size_buf, sizeof(size_buf));
    snprintf(buf, sizeof(buf), "Files: %d | Total: %s | Sort: %s",
             num_files, size_buf, sort_by_time ? "TIME" : "NAME");
    addstr(buf);
    clrtoeol();
}

static void draw_footer(void) {
    move(LINES - 2, 0);
    attron(A_REVERSE);
    addstr("j/k:Up/Dn | Enter:Open | t:Sort | L:Load | c:CRC32 | d:Del | n:NewDir | ESC:Exit");
    for (int i = 84; i < COLS; i++) addch(' ');
    standend();
}

static void draw_file_list(int selected, int scroll_offset) {
    int display_rows = LINES - 5;  // Header(3) + Footer(2)
    int start_row = 3;

    for (int i = 0; i < display_rows; i++) {
        int file_idx = scroll_offset + i;
        move(start_row + i, 0);
        clrtoeol();

        if (file_idx >= num_files) continue;

        FileEntry *entry = &file_list[file_idx];

        // Highlight selected
        if (file_idx == selected) {
            attron(A_REVERSE);
        }

        // Format: "[D] name       size         date time"
        char line[256];
        char size_buf[16];
        char date_buf[32];

        if (entry->is_dir) {
            snprintf(size_buf, sizeof(size_buf), "<DIR>");
        } else {
            format_size(entry->size, size_buf, sizeof(size_buf));
        }

        format_datetime(entry->date, entry->time, date_buf, sizeof(date_buf));

        snprintf(line, sizeof(line), "%c %-12s %12s  %s",
                 entry->is_dir ? 'D' : 'F',
                 entry->name,
                 size_buf,
                 date_buf);

        addstr(line);

        if (file_idx == selected) {
            standend();
        }
    }
}

//==============================================================================
// Action Functions
//==============================================================================

static void show_crc32(int selected) {
    if (selected < 0 || selected >= num_files) return;
    if (file_list[selected].is_dir) return;

    // Following help.c pattern - clear and show popup
    clear();
    move(0, 0);
    attron(A_REVERSE);
    addstr("=== CRC32 CHECKSUM ===");
    standend();

    move(2, 0);
    char buf[128];
    snprintf(buf, sizeof(buf), "File: %s", file_list[selected].name);
    addstr(buf);

    move(3, 0);
    addstr("Calculating CRC32...");
    refresh();

    // Build full path
    char fullpath[512];
    if (strcmp(current_path, "/") == 0) {
        snprintf(fullpath, sizeof(fullpath), "/%s", file_list[selected].name);
    } else {
        snprintf(fullpath, sizeof(fullpath), "%s/%s", current_path, file_list[selected].name);
    }

    crc32_init();
    uint32_t crc = calculate_file_crc32(fullpath);

    // Clear the entire screen and redraw to ensure clean display
    clear();
    move(0, 0);
    attron(A_REVERSE);
    addstr("=== CRC32 CHECKSUM ===");
    standend();

    move(2, 0);
    snprintf(buf, sizeof(buf), "File: %s", file_list[selected].name);
    addstr(buf);

    // Show CRC32 result with highlighting
    move(4, 0);
    attron(A_REVERSE);
    snprintf(buf, sizeof(buf), "CRC32: 0x%08lX", (unsigned long)crc);
    addstr(buf);
    standend();

    // Show file size
    move(5, 0);
    char size_buf[32];
    format_size(file_list[selected].size, size_buf, sizeof(size_buf));
    snprintf(buf, sizeof(buf), "Size: %s", size_buf);
    addstr(buf);

    move(7, 0);
    addstr("Press any key to continue...");
    refresh();

    // Simple blocking read - MUST wait here
    while (1) {
        flushinp();
        timeout(-1);
        int key = getch();
        if (key != ERR) break;  // Got a real key, exit
    }
}

static void delete_file(int selected) {
    if (selected < 0 || selected >= num_files) return;

    FileEntry *entry = &file_list[selected];

    // Don't allow deletion of ".." entry
    if (strcmp(entry->name, "..") == 0) {
        return;
    }

    // Show confirmation dialog
    clear();
    move(0, 0);
    attron(A_REVERSE);
    addstr("=== CONFIRM DELETE ===");
    standend();

    move(2, 0);
    char buf[128];
    snprintf(buf, sizeof(buf), "Delete: %s", entry->name);
    addstr(buf);

    move(3, 0);
    if (entry->is_dir) {
        addstr("Type: Directory");
    } else {
        char size_buf[32];
        format_size(entry->size, size_buf, sizeof(size_buf));
        snprintf(buf, sizeof(buf), "Size: %s", size_buf);
        addstr(buf);
    }

    move(5, 0);
    attron(A_REVERSE);
    addstr("Are you sure? (y/n)");
    standend();
    refresh();

    // Wait for y/n response
    int ch;
    while (1) {
        flushinp();
        timeout(-1);
        ch = getch();
        if (ch != ERR) break;  // Got a real key
    }

    if (ch == 'y' || ch == 'Y') {
        // Build full path
        char fullpath[512];
        if (strcmp(current_path, "/") == 0) {
            snprintf(fullpath, sizeof(fullpath), "/%s", entry->name);
        } else {
            snprintf(fullpath, sizeof(fullpath), "%s/%s", current_path, entry->name);
        }

        FRESULT fr = f_unlink(fullpath);

        move(7, 0);
        if (fr == FR_OK) {
            attron(A_REVERSE);
            addstr("✓ Deleted successfully");
            standend();
        } else {
            snprintf(buf, sizeof(buf), "✗ Error: FRESULT=%d", fr);
            addstr(buf);
        }

        move(9, 0);
        addstr("Press any key to continue...");
        refresh();

        // Wait for keypress
        while (1) {
            flushinp();
            timeout(-1);
            int key = getch();
            if (key != ERR) break;
        }

        // Rescan directory
        scan_directory(current_path);
    } else {
        // User cancelled - don't rescan
        return;
    }
}

static void create_directory(void) {
    clear();
    move(0, 0);
    attron(A_REVERSE);
    addstr("=== CREATE DIRECTORY ===");
    standend();

    move(2, 0);
    addstr("Note: Long filenames supported (up to 255 characters)");
    move(3, 0);
    addstr("Directory name: ");
    refresh();

    // Simple input (just read characters)
    char name[256];
    int pos = 0;
    flushinp();
    timeout(-1);

    while (pos < 255) {
        int ch = getch();
        if (ch == '\n' || ch == '\r') {
            break;
        } else if (ch == 127 || ch == 8) {  // Backspace
            if (pos > 0) {
                pos--;
                move(3, 16 + pos);
                addch(' ');
                move(3, 16 + pos);
                refresh();
            }
        } else if (ch >= 32 && ch < 127) {
            name[pos++] = ch;
            addch(ch);
            refresh();
        }
    }
    name[pos] = '\0';

    if (pos == 0) {
        return;  // Cancelled
    }

    // Build full path
    char fullpath[512];
    if (strcmp(current_path, "/") == 0) {
        snprintf(fullpath, sizeof(fullpath), "/%s", name);
    } else {
        snprintf(fullpath, sizeof(fullpath), "%s/%s", current_path, name);
    }

    FRESULT fr = f_mkdir(fullpath);

    move(5, 0);
    if (fr == FR_OK) {
        attron(A_REVERSE);
        addstr("✓ Directory created successfully");
        standend();
    } else {
        char buf[64];
        snprintf(buf, sizeof(buf), "✗ Error: FRESULT=%d", fr);
        addstr(buf);
    }

    move(7, 0);
    addstr("Press any key to continue...");
    refresh();

    // Wait for keypress
    while (1) {
        flushinp();
        timeout(-1);
        int key = getch();
        if (key != ERR) break;
    }

    // Rescan directory
    scan_directory(current_path);
}

// Parse hex string to uint32_t (from spi_test.c pattern)
static uint32_t parse_hex_address(const char *input) {
    uint32_t result = 0;

    // Extract only hex digits (skip spaces, "0x" prefix, etc.)
    for (int i = 0; input[i] != '\0'; i++) {
        char c = input[i];
        if (c >= '0' && c <= '9') {
            result = (result << 4) | (c - '0');
        } else if (c >= 'a' && c <= 'f') {
            result = (result << 4) | (c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            result = (result << 4) | (c - 'A' + 10);
        }
        // Skip everything else (spaces, 'x', etc.)
    }

    return result;
}

static void load_to_address(int selected) {
    if (selected < 0 || selected >= num_files) return;
    if (file_list[selected].is_dir) return;

    // Following help.c pattern - clear and show prompt
    clear();
    move(0, 0);
    attron(A_REVERSE);
    addstr("=== LOAD FILE TO MEMORY ADDRESS ===");
    standend();

    move(2, 0);
    char buf[128];
    snprintf(buf, sizeof(buf), "File: %s", file_list[selected].name);
    addstr(buf);

    move(3, 0);
    char size_buf[32];
    format_size(file_list[selected].size, size_buf, sizeof(size_buf));
    snprintf(buf, sizeof(buf), "Size: %s", size_buf);
    addstr(buf);

    move(5, 0);
    attron(A_REVERSE);
    addstr("Enter hex address (e.g., 0x80000, 80000, 8 0000):");
    standend();
    move(6, 0);
    addstr("Address: ");
    refresh();

    // Hex input (from spi_test.c pattern)
    char input_buf[32] = {0};
    int input_pos = 0;

    flushinp();
    timeout(-1);

    // Input loop
    while (input_pos < 31) {
        int ch = getch();

        if (ch == '\n' || ch == '\r') {
            // Enter - parse and load
            break;
        } else if (ch == 27) {  // ESC - cancel
            return;
        } else if (ch == 127 || ch == 8) {  // Backspace
            if (input_pos > 0) {
                input_pos--;
                input_buf[input_pos] = '\0';
                move(6, 9 + input_pos);
                addch(' ');
                move(6, 9 + input_pos);
                refresh();
            }
        } else if (ch >= 32 && ch < 127) {
            // Accept hex digits, spaces, 'x' for "0x"
            input_buf[input_pos++] = ch;
            input_buf[input_pos] = '\0';
            addch(ch);
            refresh();
        }
    }

    if (input_pos == 0) {
        return;  // Cancelled
    }

    // Parse hex address
    uint32_t address = parse_hex_address(input_buf);

    move(8, 0);
    snprintf(buf, sizeof(buf), "Parsed address: 0x%08lX", (unsigned long)address);
    addstr(buf);

    move(9, 0);
    attron(A_REVERSE);
    addstr("Load to this address? (y/n)");
    standend();
    refresh();

    // Wait for y/n response
    int confirm;
    while (1) {
        flushinp();
        timeout(-1);
        confirm = getch();
        if (confirm != ERR) break;
    }

    if (confirm != 'y' && confirm != 'Y') {
        return;  // Cancelled
    }

    // Build full path
    char fullpath[512];
    if (strcmp(current_path, "/") == 0) {
        snprintf(fullpath, sizeof(fullpath), "/%s", file_list[selected].name);
    } else {
        snprintf(fullpath, sizeof(fullpath), "%s/%s", current_path, file_list[selected].name);
    }

    // Open file
    FIL file;
    FRESULT fr = f_open(&file, fullpath, FA_READ);
    if (fr != FR_OK) {
        move(11, 0);
        snprintf(buf, sizeof(buf), "✗ Error opening file: FRESULT=%d", fr);
        addstr(buf);
        move(13, 0);
        addstr("Press any key to continue...");
        refresh();

        // Wait for keypress
        while (1) {
            flushinp();
            timeout(-1);
            int key = getch();
            if (key != ERR) break;
        }
        return;
    }

    // Load file to memory
    move(11, 0);
    addstr("Loading...");
    refresh();

    uint8_t *dest = (uint8_t *)address;
    uint8_t buffer[512];
    UINT br;
    uint32_t total_bytes = 0;

    while (1) {
        fr = f_read(&file, buffer, sizeof(buffer), &br);
        if (fr != FR_OK || br == 0) break;

        // Copy to memory
        for (UINT i = 0; i < br; i++) {
            dest[total_bytes + i] = buffer[i];
        }
        total_bytes += br;
    }

    f_close(&file);

    move(11, 0);
    clrtoeol();
    if (fr == FR_OK || total_bytes > 0) {
        attron(A_REVERSE);
        snprintf(buf, sizeof(buf), "✓ Loaded %lu bytes to 0x%08lX",
                 (unsigned long)total_bytes, (unsigned long)address);
        addstr(buf);
        standend();
    } else {
        snprintf(buf, sizeof(buf), "✗ Error reading file: FRESULT=%d", fr);
        addstr(buf);
    }

    move(13, 0);
    addstr("Press any key to continue...");
    refresh();

    // Wait for keypress
    while (1) {
        flushinp();
        timeout(-1);
        int key = getch();
        if (key != ERR) break;
    }
}

static void enter_directory(int selected) {
    if (selected < 0 || selected >= num_files) return;
    if (!file_list[selected].is_dir) return;

    // Handle ".." (parent directory)
    if (strcmp(file_list[selected].name, "..") == 0) {
        // Go up one level
        char *last_slash = strrchr(current_path, '/');
        if (last_slash && last_slash != current_path) {
            *last_slash = '\0';
        } else {
            strcpy(current_path, "/");
        }
    } else {
        // Enter subdirectory
        if (strcmp(current_path, "/") == 0) {
            snprintf(current_path, sizeof(current_path), "/%s", file_list[selected].name);
        } else {
            char temp[256];
            snprintf(temp, sizeof(temp), "%s/%s", current_path, file_list[selected].name);
            strncpy(current_path, temp, sizeof(current_path) - 1);
        }
    }

    scan_directory(current_path);
}

//==============================================================================
// Main File Browser Function
//==============================================================================

void show_file_browser(void) {
    // Flush any pending input before starting
    // Note: flushinp() is a no-op in incurses, so we flush manually
    timeout(0);
    while (getch() != ERR);
    timeout(-1);

    if (!g_card_mounted) {
        clear();
        move(0, 0);
        addstr("Error: SD card not mounted!");
        move(2, 0);
        addstr("Press any key to return...");
        refresh();

        // Wait for keypress
        while (1) {
            int key = getch();
            if (key != ERR) break;
        }
        return;
    }

    // Initialize CRC32
    crc32_init();

    // Scan initial directory
    if (scan_directory(current_path) < 0) {
        clear();
        move(0, 0);
        addstr("Error: Cannot read directory!");
        move(2, 0);
        addstr("Press any key to return...");
        refresh();

        // Wait for keypress
        while (1) {
            int key = getch();
            if (key != ERR) break;
        }
        return;
    }

    int selected = 0;
    int scroll_offset = 0;
    int display_rows = LINES - 5;
    int need_redraw = 1;

    while (1) {
        if (need_redraw) {
            clear();
            draw_header();
            draw_file_list(selected, scroll_offset);
            draw_footer();
            refresh();
            need_redraw = 0;
        }

        // Ensure proper input mode (subfunctions may change it)
        timeout(-1);
        int ch = get_key_with_arrows();  // Use helper to detect arrow keys

        // Handle input
        if (ch == 27) {  // ESC - exit
            break;
        } else if (ch == 'j' || ch == KEY_DOWN) {  // Down
            if (selected < num_files - 1) {
                selected++;
                if (selected >= scroll_offset + display_rows) {
                    scroll_offset++;
                }
                need_redraw = 1;
            }
        } else if (ch == 'k' || ch == KEY_UP) {  // Up
            if (selected > 0) {
                selected--;
                if (selected < scroll_offset) {
                    scroll_offset--;
                }
                need_redraw = 1;
            }
        } else if (ch == '\n' || ch == '\r') {  // Enter - open directory
            enter_directory(selected);
            selected = 0;
            scroll_offset = 0;
            need_redraw = 1;
        } else if (ch == 't' || ch == 'T') {  // Toggle sort
            sort_by_time = !sort_by_time;
            sort_file_list();
            need_redraw = 1;
        } else if (ch == 'c' || ch == 'C') {  // CRC32
            show_crc32(selected);
            need_redraw = 1;
        } else if (ch == 'd' || ch == 'D') {  // Delete
            delete_file(selected);
            if (selected >= num_files) {
                selected = num_files - 1;
            }
            if (selected < 0) selected = 0;
            need_redraw = 1;
        } else if (ch == 'n' || ch == 'N') {  // New directory
            create_directory();
            need_redraw = 1;
        } else if (ch == 'l' || ch == 'L') {  // Load to address
            load_to_address(selected);
            need_redraw = 1;
        }
    }
}
