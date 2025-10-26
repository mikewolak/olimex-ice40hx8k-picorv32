# Overlay System - Implementation Plan

## Quick Start Summary

**Goal**: Enable uploading position-independent code modules to SD card via UART, then load and execute them from RAM.

**Why**: Extend functionality without reflashing FPGA. Store multiple apps on SD card, load on demand.

**How**: Reuse proven bootloader upload protocol, store to SD card, load to heap (0x50000), execute as PIC.

---

## Phase 1: Upload to SD Card (START HERE)

### Files to Create

#### 1. `overlay_upload.h` - Upload interface
```c
#ifndef OVERLAY_UPLOAD_H
#define OVERLAY_UPLOAD_H

#include <stdint.h>
#include "ff.h"

// Upload overlay via UART, save to SD card
// Returns: bytes uploaded, or -1 on error
int32_t upload_overlay_to_sd(const char *filename);

// Display upload progress
void show_upload_progress(uint32_t bytes, uint32_t total);

#endif
```

#### 2. `overlay_upload.c` - Implementation
```c
#include "overlay_upload.h"
#include "simple_upload.h"
#include "io.h"
#include "../../lib/incurses/curses.h"
#include <stdlib.h>
#include <string.h>

#define MAX_OVERLAY_SIZE (192 * 1024)  // 192 KB

static uint8_t *upload_buffer = NULL;

// Callbacks for simple_upload
static uint8_t upload_getc(void) {
    return (uint8_t)uart_getc();
}

static void upload_putc(uint8_t c) {
    uart_putc((char)c);
}

static simple_callbacks_t upload_callbacks = {
    .getc = upload_getc,
    .putc = upload_putc
};

int32_t upload_overlay_to_sd(const char *filename) {
    FIL file;
    FRESULT res;
    UINT bytes_written;
    int32_t bytes_received;

    // Allocate buffer if needed
    if (!upload_buffer) {
        upload_buffer = malloc(MAX_OVERLAY_SIZE);
        if (!upload_buffer) {
            return -1;  // Out of memory
        }
    }

    // Clear screen and show instructions
    clear();
    move(0, 0);
    addstr("===========================================");
    move(1, 0);
    addstr("  OVERLAY UPLOAD - UART Protocol");
    move(2, 0);
    addstr("===========================================");
    move(4, 0);
    addstr("On PC, run:");
    move(5, 2);
    attron(A_REVERSE);
    addstr("fw_upload_fast -p /dev/ttyUSB0 overlay.bin");
    standend();
    move(7, 0);
    addstr("Waiting for upload... (ESC to cancel)");
    refresh();

    // Receive via UART using proven protocol
    bytes_received = simple_receive(&upload_callbacks, upload_buffer, MAX_OVERLAY_SIZE);

    if (bytes_received < 0) {
        move(9, 0);
        addstr("✗ Upload failed or cancelled");
        refresh();
        getch();
        return bytes_received;
    }

    // Show success
    move(9, 0);
    char msg[80];
    snprintf(msg, sizeof(msg), "✓ Received %ld bytes, CRC verified", bytes_received);
    addstr(msg);
    refresh();

    // Create /OVERLAYS directory if needed
    f_mkdir("/OVERLAYS");

    // Build full path
    char fullpath[32];
    snprintf(fullpath, sizeof(fullpath), "/OVERLAYS/%s", filename);

    // Save to SD card
    res = f_open(&file, fullpath, FA_CREATE_ALWAYS | FA_WRITE);
    if (res != FR_OK) {
        move(10, 0);
        addstr("✗ Failed to open file on SD card");
        refresh();
        getch();
        return -1;
    }

    res = f_write(&file, upload_buffer, bytes_received, &bytes_written);
    f_close(&file);

    if (res != FR_OK || bytes_written != (UINT)bytes_received) {
        move(10, 0);
        addstr("✗ Failed to write file to SD card");
        refresh();
        getch();
        return -1;
    }

    // Success!
    move(10, 0);
    snprintf(msg, sizeof(msg), "✓ Saved to %s", fullpath);
    addstr(msg);
    move(12, 0);
    addstr("Press any key to continue...");
    refresh();
    getch();

    return bytes_received;
}
```

#### 3. Update `sd_card_manager.c` - Add menu option

In the menu enum:
```c
#define MENU_DETECT_CARD    0
#define MENU_CARD_INFO      1
#define MENU_FORMAT_CARD    2
#define MENU_FILE_BROWSER   3
#define MENU_CREATE_FILE    4
#define MENU_BENCHMARK      5
#define MENU_SPI_SPEED      6
#define MENU_EJECT_CARD     7
#define MENU_UPLOAD_OVERLAY 8   // NEW
#define NUM_MENU_OPTIONS    9   // NEW (was 8)
```

In the menu array:
```c
const char *menu_options[NUM_MENU_OPTIONS] = {
    "Detect SD Card",
    "Card Information",
    "Format Card (FAT32)",
    "File Browser",
    "Create Test File",
    "Read/Write Benchmark",
    "SPI Speed Configuration",
    "Eject Card",
    "Upload Overlay (UART)"  // NEW
};
```

In main loop:
```c
} else if (ch == '\n' || ch == '\r') {
    // Handle menu selection
    if (selected_menu == MENU_UPLOAD_OVERLAY) {
        char filename[13];  // 8.3 format

        // Prompt for filename
        echo();
        curs_set(1);
        move(LINES - 1, 0);
        addstr("Filename (8.3): ");
        getstr(filename);
        noecho();
        curs_set(0);

        // Upload
        upload_overlay_to_sd(filename);

        need_full_redraw = 1;
    }
    // ... other menu handlers ...
}
```

#### 4. Update `Makefile` - Add simple_upload

Already exists in `lib/simple_upload/`, just need to link it:

```makefile
# In sd_fatfs/Makefile
PROJECT_SOURCES = sd_card_manager.c sd_spi.c diskio.c io.c help.c overlay_upload.c

# In firmware/Makefile, add to SD_FATFS_OBJS:
SD_FATFS_OBJS = $(SD_FATFS_DIR)/sd_card_manager.o \
                 $(SD_FATFS_DIR)/sd_spi.o \
                 $(SD_FATFS_DIR)/diskio.o \
                 $(SD_FATFS_DIR)/io.o \
                 $(SD_FATFS_DIR)/help.o \
                 $(SD_FATFS_DIR)/overlay_upload.o \
                 $(SIMPLE_UPLOAD_OBJ) \
                 $(SD_FATFS_DIR)/fatfs/source/ff.o
```

---

## Phase 2: Overlay Build System

### Create `firmware/overlays/` directory

```bash
mkdir -p firmware/overlays/template
```

### Files to Create

#### 1. `overlays/overlay.h` - Common overlay header
```c
#ifndef OVERLAY_H
#define OVERLAY_H

#include <stdint.h>

// Standard overlay entry point
// Every overlay MUST provide this function
void overlay_entry(void) __attribute__((section(".text.overlay_entry")));

// Hardware access (re-export from main firmware)
extern void uart_putc(char c);
extern char uart_getc(void);
extern void led_on(uint8_t led);
extern void led_off(uint8_t led);

#endif
```

#### 2. `overlays/overlay_linker.ld` - PIC linker script
```ld
MEMORY {
    OVERLAY (rwx) : ORIGIN = 0x00050000, LENGTH = 0x00030000
}

SECTIONS {
    . = 0x00050000;

    .text : {
        *(.text.overlay_entry)
        *(.text*)
        . = ALIGN(4);
    } > OVERLAY

    .rodata : {
        *(.rodata*)
        . = ALIGN(4);
    } > OVERLAY

    .data : {
        *(.data*)
        . = ALIGN(4);
    } > OVERLAY

    .bss : {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > OVERLAY

    __overlay_end = .;
}
```

#### 3. `overlays/Makefile` - Build system
```makefile
CC = riscv64-unknown-elf-gcc
OBJCOPY = riscv64-unknown-elf-objcopy

CFLAGS = -march=rv32im -mabi=ilp32 -O2 -g -Wall \
         -fPIC -fno-plt -fno-jump-tables -mno-relax \
         -ffreestanding -fno-builtin

LDFLAGS = -T overlay_linker.ld -nostartfiles -static -Wl,-Bsymbolic

all: hello_overlay.bin

hello_overlay.elf: template/hello_overlay.c
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

%.bin: %.elf
	$(OBJCOPY) -O binary $< $@
	@ls -lh $@

clean:
	rm -f *.elf *.bin *.o
```

#### 4. `overlays/template/hello_overlay.c` - Template
```c
#include "../overlay.h"

// Minimal UART functions (inline, no linking needed)
static inline void putc(char c) {
    volatile uint32_t *uart_tx_data = (volatile uint32_t *)0x80000000;
    volatile uint32_t *uart_tx_status = (volatile uint32_t *)0x80000004;
    while (*uart_tx_status & 1);
    *uart_tx_data = c;
}

static void puts(const char *s) {
    while (*s) {
        if (*s == '\n') putc('\r');
        putc(*s++);
    }
}

// Entry point - MUST be named overlay_entry
void __attribute__((section(".text.overlay_entry"))) overlay_entry(void) {
    puts("\n\n=================================\n");
    puts("   Hello from Overlay!\n");
    puts("   Running at 0x50000\n");
    puts("=================================\n\n");

    // Blink LED
    volatile uint32_t *led = (volatile uint32_t *)0x80000010;
    for (int i = 0; i < 10; i++) {
        *led = 1;
        for (volatile int j = 0; j < 1000000; j++);
        *led = 0;
        for (volatile int j = 0; j < 1000000; j++);
    }

    puts("Overlay complete. Returning to SD Manager.\n");

    // Return to SD manager
    return;
}
```

### Build Test Overlay

```bash
cd firmware/overlays
make hello_overlay.bin
```

Should produce: `hello_overlay.bin` (~500 bytes)

### Upload to SD Card

```bash
# From PC:
cd tools/uploader
./fw_upload_fast -p /dev/ttyUSB0 ../../firmware/overlays/hello_overlay.bin

# On FPGA:
# SD Manager menu → Upload Overlay
# Enter filename: HELLO.BIN
```

---

## Phase 3: Overlay Execution

### Files to Create

#### 1. `overlay_loader.h`
```c
#ifndef OVERLAY_LOADER_H
#define OVERLAY_LOADER_H

#include <stdint.h>

// Load overlay from SD card to execution memory
// Returns: size loaded, or -1 on error
int32_t load_overlay(const char *filename);

// Execute loaded overlay
// Returns: 0 on success, -1 on error
int run_overlay(void);

// List available overlays
void browse_overlays(void);

#endif
```

#### 2. `overlay_loader.c`
```c
#include "overlay_loader.h"
#include "ff.h"
#include "../../lib/incurses/curses.h"
#include <stdlib.h>
#include <string.h>

#define OVERLAY_LOAD_ADDR   0x00050000
#define MAX_OVERLAY_SIZE    (192 * 1024)

static uint32_t loaded_size = 0;

int32_t load_overlay(const char *filename) {
    FIL file;
    FRESULT res;
    UINT bytes_read;
    char fullpath[32];
    uint8_t *overlay_mem = (uint8_t *)OVERLAY_LOAD_ADDR;

    // Build path
    snprintf(fullpath, sizeof(fullpath), "/OVERLAYS/%s", filename);

    // Open file
    res = f_open(&file, fullpath, FA_READ);
    if (res != FR_OK) {
        return -1;
    }

    // Read to overlay memory
    res = f_read(&file, overlay_mem, MAX_OVERLAY_SIZE, &bytes_read);
    f_close(&file);

    if (res != FR_OK) {
        return -1;
    }

    loaded_size = bytes_read;
    return bytes_read;
}

int run_overlay(void) {
    if (loaded_size == 0) {
        return -1;  // No overlay loaded
    }

    // Function pointer to overlay entry
    typedef void (*overlay_func_t)(void);
    overlay_func_t overlay_entry = (overlay_func_t)OVERLAY_LOAD_ADDR;

    // Clean up ncurses before jumping
    endwin();

    // Jump to overlay
    overlay_entry();

    // Reinitialize ncurses when overlay returns
    initscr();
    noecho();
    raw();
    keypad(stdscr, TRUE);
    curs_set(0);

    return 0;
}

void browse_overlays(void) {
    DIR dir;
    FILINFO fno;
    FRESULT res;
    int count = 0;

    clear();
    move(0, 0);
    addstr("Available Overlays:");
    move(1, 0);
    addstr("==================");

    res = f_opendir(&dir, "/OVERLAYS");
    if (res != FR_OK) {
        move(3, 0);
        addstr("No overlays directory");
        refresh();
        getch();
        return;
    }

    int row = 3;
    while (1) {
        res = f_readdir(&dir, &fno);
        if (res != FR_OK || fno.fname[0] == 0) break;

        // Skip directories and .INF files
        if (fno.fattrib & AM_DIR) continue;
        if (strstr(fno.fname, ".INF")) continue;

        move(row++, 2);
        char info[80];
        snprintf(info, sizeof(info), "%s (%lu bytes)", fno.fname, fno.fsize);
        addstr(info);
        count++;
    }

    f_closedir(&dir);

    if (count == 0) {
        move(3, 0);
        addstr("No overlays found. Upload some first!");
    }

    move(LINES - 1, 0);
    addstr("Press any key to continue...");
    refresh();
    getch();
}
```

#### 3. Add to `sd_card_manager.c`

```c
#define MENU_BROWSE_OVERLAYS 9
#define MENU_RUN_OVERLAY    10
#define NUM_MENU_OPTIONS    11
```

```c
const char *menu_options[] = {
    // ... existing options ...
    "Upload Overlay (UART)",
    "Browse Overlays",       // NEW
    "Run Overlay"            // NEW
};
```

```c
// In main loop:
if (selected_menu == MENU_BROWSE_OVERLAYS) {
    browse_overlays();
    need_full_redraw = 1;
} else if (selected_menu == MENU_RUN_OVERLAY) {
    char filename[13];

    echo();
    curs_set(1);
    move(LINES - 1, 0);
    addstr("Overlay filename: ");
    getstr(filename);
    noecho();
    curs_set(0);

    int32_t size = load_overlay(filename);
    if (size > 0) {
        run_overlay();
    } else {
        clear();
        move(LINES/2, 0);
        addstr("Failed to load overlay!");
        refresh();
        getch();
    }

    need_full_redraw = 1;
}
```

---

## Testing Checklist

### Phase 1 Tests
- [ ] Build sd_card_manager with upload support
- [ ] Upload test file via fw_upload_fast
- [ ] Verify file saved to /OVERLAYS/ on SD card
- [ ] Check file size matches upload size
- [ ] Test upload cancel (ESC key)

### Phase 2 Tests
- [ ] Build hello_overlay.bin
- [ ] Verify binary is position-independent (no absolute addresses)
- [ ] Check binary size < 1 KB
- [ ] Disassemble and verify no GOT/PLT references

### Phase 3 Tests
- [ ] Load HELLO.BIN from SD to 0x50000
- [ ] Run overlay, verify UART output
- [ ] Verify LED blinks 10 times
- [ ] Confirm return to SD manager
- [ ] Browse overlays, verify listing

---

## Quick Start Commands

```bash
# 1. Build SD manager with overlay support
cd firmware
make clean
make sd_card_manager

# 2. Build test overlay
cd overlays
make hello_overlay.bin

# 3. Upload to FPGA
cd ../..
tools/uploader/fw_upload_fast -p /dev/ttyUSB0 firmware/sd_card_manager.bin

# 4. Connect to FPGA
minicom -D /dev/ttyUSB0 -b 115200

# 5. In SD Manager:
#    - Detect SD Card
#    - Upload Overlay → enter "HELLO.BIN"
#
# 6. On PC (in new terminal):
cd tools/uploader
./fw_upload_fast -p /dev/ttyUSB0 ../../firmware/overlays/hello_overlay.bin

# 7. In SD Manager:
#    - Browse Overlays → see HELLO.BIN
#    - Run Overlay → enter "HELLO.BIN"
#    - Watch LED blink!
```

---

## Next Steps After Phase 3

1. **Port hexedit_fast to overlay**
2. **Add CRC verification** before execution
3. **Create .INF metadata files** during upload
4. **Add overlay selection menu** (instead of typing filename)
5. **Implement overlay delete**
6. **Add overlay info display**

---

## Troubleshooting

### Overlay doesn't run
- Check overlay is loaded to 0x50000 (add debug prints)
- Verify overlay_entry is at 0x50000 (check .elf with `objdump`)
- Make sure overlay returns (infinite loop will hang)

### Overlay crashes
- Check stack usage (overlays share stack with SD manager)
- Verify no absolute addresses in overlay code
- Use `-fPIC` flag during compilation

### Upload fails
- Verify fw_upload_fast matches simple_upload protocol
- Check CRC calculation is correct
- Ensure SD card has free space

---

## Author

Michael Wolak (mikewolak@gmail.com)
October 2025
