# Overlay SDK Design Document

## Overview

The Overlay SDK provides a complete development environment for creating position-independent overlays that can be loaded and executed from the SD Card Manager.

## Key Differences: Bootloader vs Overlay

### Bootloader (Reference: bootloader_fast.c)
- **Load Address**: 0x40000 (BRAM/ROM)
- **Execution**: Fixed location, never moves
- **Purpose**: Upload firmware to 0x0, then jump to it
- **Return**: Never returns (jumps to firmware)
- **Libraries**: Bare metal, no libc
- **Stack**: 256 bytes at 0x42000
- **Compilation**: Standard `-nostdlib -nostartfiles`

### Overlay (New Requirements)
- **Load Address**: 0x18000 (SRAM)
- **Execution**: Position-independent, can run anywhere
- **Purpose**: Run application code, then return to SD Manager
- **Return**: MUST cleanly return to caller via `exit()` or `return from main()`
- **Libraries**: Full newlib (printf, etc.), incurses, FatFS (all PIC)
- **Memory Layout**:
  ```
  0x18000 - 0x37FFF : Code/Data/BSS (128 KB)
  0x38000 - 0x39FFF : Stack (8 KB, grows down from 0x3A000)
  0x3A000+          : Heap (grows up)
  ```
- **Compilation**: `-fPIC -fno-plt -nostartfiles` + PIC libraries

## Memory Layout Detail

```
┌─────────────────────────────────────┐ 0x18000
│  Overlay Code (.text)               │
├─────────────────────────────────────┤
│  Read-only Data (.rodata)           │
├─────────────────────────────────────┤
│  Initialized Data (.data)           │
├─────────────────────────────────────┤
│  Uninitialized Data (.bss)          │
├─────────────────────────────────────┤ 0x38000 (128KB limit)
│  Stack (8KB, grows DOWN)            │
│  ↓↓↓                                │
├─────────────────────────────────────┤ 0x3A000
│  Heap (grows UP)                    │
│  ↑↑↑                                │
└─────────────────────────────────────┘ 0x42000 (heap end)
```

## Directory Structure

```
firmware/overlay_sdk/
├── Makefile.overlay         # Master makefile with shared compiler settings
├── common/
│   ├── overlay_start.S      # Position-independent startup code
│   ├── overlay_linker.ld    # Linker script for 0x18000 base
│   ├── hardware.h           # MMIO register definitions
│   ├── io.h                 # I/O helper function declarations
│   └── io.c                 # I/O helper function implementations
├── sysroot_pic/             # PIC-compiled libraries
│   ├── riscv64-unknown-elf/
│   │   ├── lib/
│   │   │   ├── libc.a       # Newlib (PIC)
│   │   │   ├── libm.a       # Math library (PIC)
│   │   │   └── libcurses.a  # incurses (PIC)
│   │   └── include/         # Headers
│   └── build.sh             # Script to build PIC sysroot
├── templates/
│   ├── Makefile.template    # Template for project Makefiles
│   └── main.c.template      # Template for main.c
└── projects/
    ├── hello_world/         # Example/test overlay
    │   ├── Makefile
    │   └── main.c
    └── <user_project>/      # User projects created by 'make new_overlay'
```

## Critical Compilation Flags

### For Overlay Code
```makefile
ARCH = rv32im
ABI = ilp32
CFLAGS = -march=$(ARCH) -mabi=$(ABI) -O2 -g
CFLAGS += -fPIC -fno-plt              # Position-independent code
CFLAGS += -ffreestanding -fno-builtin
CFLAGS += -Wall -Wextra
CFLAGS += -nostartfiles               # We provide our own start.S
```

### For PIC Libraries (Sysroot)
```makefile
# When building newlib/incurses for overlays
CFLAGS_FOR_TARGET = -fPIC -fno-plt -O2 -g
```

## overlay_start.S Requirements

Unlike bootloader's start.S, overlay startup must:

1. **Be position-independent**: Use `auipc` + offset, not absolute `la`
2. **Set up stack**: Point SP to 0x3A000 (top of 8KB stack)
3. **Clear BSS**: Using position-independent addressing
4. **Call main()**: Standard C main function
5. **Handle return**: If main() returns, call `_exit()` to return to SD Manager
6. **No IRQ vector**: Overlays don't handle interrupts (SD Manager does)

```assembly
.section .text.start
.global _start

_start:
    # Set stack pointer (fixed at 0x3A000)
    lui sp, 0x3A              # sp = 0x3A000

    # Clear BSS (position-independent)
    # Use PC-relative addressing
    auipc t0, %pcrel_hi(__bss_start)
    addi t0, t0, %pcrel_lo(_start)
    auipc t1, %pcrel_hi(__bss_end)
    addi t1, t1, %pcrel_lo(_start)

clear_bss:
    bge t0, t1, done_bss
    sw zero, 0(t0)
    addi t0, t0, 4
    j clear_bss
done_bss:

    # Call main()
    call main

    # If main returns, call exit(0)
    li a0, 0
    call exit
```

## overlay_linker.ld Design

Key differences from bootloader linker:

```ld
MEMORY
{
    OVERLAY (rwx) : ORIGIN = 0x00018000, LENGTH = 128K
    STACK (rw)    : ORIGIN = 0x00038000, LENGTH = 8K
    HEAP (rw)     : ORIGIN = 0x0003A000, LENGTH = 32K
}

SECTIONS
{
    . = 0x18000;  # Fixed overlay base

    .text : {
        *(.text.start)    # overlay_start.S must be first
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

    # Verify overlay doesn't exceed 128KB
    __overlay_size = . - 0x18000;
    ASSERT(__overlay_size <= 128K, "ERROR: Overlay exceeds 128KB!")

    # Stack grows down from 0x3A000
    __stack_top = ORIGIN(STACK) + LENGTH(STACK);

    # Heap starts after BSS, ends at stack
    __heap_start = __bss_end;
    __heap_end = ORIGIN(STACK);
}
```

## Makefile.overlay (Master)

Shared settings for all overlay projects:

```makefile
# Toolchain
PREFIX = riscv64-unknown-elf-
CC = $(PREFIX)gcc
LD = $(PREFIX)ld
OBJCOPY = $(PREFIX)objcopy
OBJDUMP = $(PREFIX)objdump

# Architecture
ARCH = rv32im
ABI = ilp32

# Overlay SDK paths
SDK_ROOT = $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
COMMON_DIR = $(SDK_ROOT)/common
SYSROOT_PIC = $(SDK_ROOT)/sysroot_pic

# Compiler flags for overlays
OVERLAY_CFLAGS = -march=$(ARCH) -mabi=$(ABI) -O2 -g
OVERLAY_CFLAGS += -fPIC -fno-plt
OVERLAY_CFLAGS += -ffreestanding -fno-builtin
OVERLAY_CFLAGS += -Wall -Wextra
OVERLAY_CFLAGS += -nostartfiles
OVERLAY_CFLAGS += -isystem $(SYSROOT_PIC)/riscv64-unknown-elf/include

# Linker flags
OVERLAY_LDFLAGS = -T $(COMMON_DIR)/overlay_linker.ld
OVERLAY_LDFLAGS += -L$(SYSROOT_PIC)/riscv64-unknown-elf/lib
OVERLAY_LDFLAGS += -static -nostartfiles
OVERLAY_LDFLAGS += -Wl,--gc-sections
OVERLAY_LDFLAGS += -lc -lm -lgcc

# Common sources
OVERLAY_START = $(COMMON_DIR)/overlay_start.S
OVERLAY_IO = $(COMMON_DIR)/io.c
```

## Project Makefile Template

Each overlay project inherits from Makefile.overlay:

```makefile
# Include master overlay settings
include ../../Makefile.overlay

# Project-specific settings
PROJECT_NAME = hello_world
SOURCES = main.c

# Targets
all: $(PROJECT_NAME).bin

$(PROJECT_NAME).elf: $(SOURCES) $(OVERLAY_START) $(OVERLAY_IO)
	$(CC) $(OVERLAY_CFLAGS) $(OVERLAY_LDFLAGS) \
		$(OVERLAY_START) $(OVERLAY_IO) $(SOURCES) \
		-o $@

$(PROJECT_NAME).bin: $(PROJECT_NAME).elf
	$(OBJCOPY) -O binary $< $@
	@ls -lh $@

clean:
	rm -f $(PROJECT_NAME).elf $(PROJECT_NAME).bin $(PROJECT_NAME).lst

.PHONY: all clean
```

## main.c Template

```c
//==============================================================================
// Overlay Project: <PROJECT_NAME>
//
// Copyright (c) October 2025
//==============================================================================

#include "hardware.h"
#include "io.h"
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    // Your overlay code here
    printf("Hello from overlay!\r\n");

    // Example: loop with exit condition
    for (int i = 0; i < 10; i++) {
        printf("Count: %d\r\n", i);

        // Simulate work
        for (volatile int j = 0; j < 1000000; j++);
    }

    printf("Overlay complete. Returning to SD Card Manager...\r\n");

    // Return to SD Card Manager
    // Option 1: return from main
    return 0;

    // Option 2: call exit explicitly
    // exit(0);
}
```

## 'make new_overlay <name>' Target

In top-level Makefile.overlay:

```makefile
.PHONY: new_overlay
new_overlay:
	@if [ -z "$(name)" ]; then \
		echo "Usage: make new_overlay name=<project_name>"; \
		exit 1; \
	fi
	@if [ -d "projects/$(name)" ]; then \
		echo "Error: Project 'projects/$(name)' already exists"; \
		exit 1; \
	fi
	@echo "Creating new overlay project: $(name)"
	@mkdir -p projects/$(name)
	@sed 's/<PROJECT_NAME>/$(name)/g' templates/Makefile.template > projects/$(name)/Makefile
	@sed 's/<PROJECT_NAME>/$(name)/g' templates/main.c.template > projects/$(name)/main.c
	@echo "*.elf" > projects/$(name)/.gitignore
	@echo "*.bin" >> projects/$(name)/.gitignore
	@echo "*.lst" >> projects/$(name)/.gitignore
	@echo "*.map" >> projects/$(name)/.gitignore
	@echo ""
	@echo "✓ Created overlay project: projects/$(name)"
	@echo "  Files:"
	@echo "    - projects/$(name)/Makefile"
	@echo "    - projects/$(name)/main.c"
	@echo ""
	@echo "Build with:"
	@echo "  cd projects/$(name) && make"
	@echo ""
```

## Building PIC Sysroot

The most complex part - need to rebuild newlib and incurses with `-fPIC`:

```bash
#!/bin/bash
# sysroot_pic/build.sh

set -e

SDK_ROOT="$(dirname "$0")/.."
SYSROOT_DIR="$SDK_ROOT/sysroot_pic"
BUILD_DIR="$SDK_ROOT/build_pic"

PREFIX=riscv64-unknown-elf-
ARCH=rv32im
ABI=ilp32

# Compiler flags for PIC libraries
CFLAGS_FOR_TARGET="-march=$ARCH -mabi=$ABI -O2 -g -fPIC -fno-plt"

# 1. Build newlib with PIC
# 2. Build incurses with PIC
# 3. Install to sysroot_pic/

# This will be a complex script - we'll implement it step by step
```

## Return Mechanism

Critical: Overlays must cleanly return to SD Card Manager.

### In Overlay:
```c
int main(void) {
    // ... overlay code ...
    return 0;  // Returns to overlay_start.S
}
```

### In overlay_start.S:
```assembly
_start:
    # ... setup ...
    call main

    # main() returned - call exit()
    li a0, 0
    call exit   # Provided by newlib, jumps to _exit
```

### In SD Card Manager (overlay_loader.c):
```c
typedef void (*overlay_func_t)(void);

void run_overlay(uint32_t entry_point) {
    overlay_func_t overlay = (overlay_func_t)entry_point;

    overlay();  // Call overlay

    // Overlay returned cleanly
    printf("Overlay returned successfully\r\n");
    // Continue with SD Card Manager menu
}
```

## Testing Plan

1. **Create hello_world overlay**:
   ```bash
   cd firmware/overlay_sdk
   make new_overlay name=hello_world
   cd projects/hello_world
   make
   ```

2. **Upload to SD card**:
   ```bash
   # From SD Card Manager menu:
   # - Upload Overlay
   # - Select hello_world.bin
   ```

3. **Load and run**:
   ```bash
   # From SD Card Manager menu:
   # - Browse and Run Overlays
   # - Select HELLO_WORLD.BIN
   ```

4. **Verify**:
   - Overlay prints "Hello from overlay!"
   - Overlay returns cleanly
   - SD Card Manager menu reappears

## Implementation Order

1. ✅ Examine bootloader patterns (DONE)
2. Create directory structure
3. Copy hardware.h and io.h
4. Create overlay_linker.ld
5. Create overlay_start.S
6. Create Makefile.overlay
7. Create templates
8. Create hello_world test project
9. Build and test WITHOUT PIC sysroot first (bare metal)
10. Build PIC sysroot
11. Rebuild hello_world with full libraries
12. Test complete flow

## Author

Michael Wolak
October 2025
