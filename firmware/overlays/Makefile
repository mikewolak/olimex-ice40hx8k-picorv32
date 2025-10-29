#===============================================================================
# Overlay Build System - Position Independent Code
#
# Builds overlay binaries that can be uploaded via UART and executed from RAM.
# All overlays are built as position-independent code (PIC) to run at 0x18000.
#
# Usage:
#   make                  - Build all overlays
#   make hello            - Build specific overlay
#   make clean            - Clean all overlays
#
# Copyright (c) October 2025 Michael Wolak
# Email: mikewolak@gmail.com, mike@epromfoundry.com
#===============================================================================

# Detect host OS and set appropriate toolchain prefix
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    PREFIX = riscv-none-elf-
else
    PREFIX = riscv64-unknown-elf-
endif

CC = $(PREFIX)gcc
OBJCOPY = $(PREFIX)objcopy
OBJDUMP = $(PREFIX)objdump
SIZE = $(PREFIX)size

# Compiler flags for RV32IM with PIC support
ARCH = rv32imc
ABI = ilp32

# CRITICAL: Position-independent code flags
CFLAGS = -march=$(ARCH) -mabi=$(ABI) -O2 -g
CFLAGS += -Wall -Wextra
CFLAGS += -ffreestanding -fno-builtin
CFLAGS += -fPIC                    # Position-independent code
CFLAGS += -fno-plt                 # No procedure linkage table
CFLAGS += -fno-jump-tables         # No jump tables (helps with PIC)
CFLAGS += -mno-relax               # Don't relax code (preserves PIC)

# Linker flags
LDFLAGS = -T overlay_linker.ld
LDFLAGS += -nostdlib -nostartfiles
LDFLAGS += -static
LDFLAGS += -Wl,--gc-sections
LDFLAGS += -Wl,-Bsymbolic          # Bind symbols locally

# Include paths
INCLUDES = -I. -I../../lib/incurses

# Overlay targets
OVERLAYS = hello.bin

# All target
all: $(OVERLAYS)
	@echo ""
	@echo "========================================="
	@echo "✓ All Overlays Built"
	@echo "========================================="
	@echo ""
	@ls -lh *.bin
	@echo ""

# Pattern rule: .c -> .elf
%.elf: %.c overlay_common.h overlay_linker.ld
	@echo "Building overlay: $@"
	$(CC) $(CFLAGS) $(INCLUDES) $(LDFLAGS) $< -o $@
	@echo ""

# Pattern rule: .elf -> .bin
%.bin: %.elf
	$(OBJCOPY) -O binary $< $@
	@echo "Binary: $@"
	$(SIZE) $<
	@echo ""

# Clean
clean:
	@echo "Cleaning overlays..."
	@rm -f *.o *.elf *.bin *.lst *.map
	@echo "✓ Clean complete"

# Disassembly (for debugging)
%.lst: %.elf
	$(OBJDUMP) -D -S $< > $@

.PHONY: all clean
