#===============================================================================
# Olimex iCE40HX8K-EVB RISC-V Platform - Bootloader Build System
# Makefile - Bootloader at 0x10000
#
# Copyright (c) October 2025 Michael Wolak
# Email: mikewolak@gmail.com, mike@epromfoundry.com
#
# NOT FOR COMMERCIAL USE
# Educational and research purposes only
#===============================================================================

# Include project configuration (ARCH, ABI from .config)
-include ../config.mk

# Detect host OS and set appropriate toolchain prefix
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    # macOS - use homebrew RISC-V toolchain
    PREFIX = riscv-none-elf-
    $(info Building bootloader on macOS - using $(PREFIX) toolchain)
else
    # Linux/Windows - use standard toolchain
    PREFIX = riscv64-unknown-elf-
    $(info Building bootloader on $(UNAME_S) - using $(PREFIX) toolchain)
endif

CC = $(PREFIX)gcc
AS = $(PREFIX)as
LD = $(PREFIX)ld
OBJCOPY = $(PREFIX)objcopy
OBJDUMP = $(PREFIX)objdump
SIZE = $(PREFIX)size

# Source files
SOURCES_STD = bootloader.c
SOURCES_FAST = bootloader_fast.c
ASM_SOURCES = start.S

# Default bootloader variant (can be overridden: make BOOTLOADER=fast)
BOOTLOADER ?= std

# Select source based on variant
ifeq ($(BOOTLOADER),fast)
    SOURCES = $(SOURCES_FAST)
    VARIANT_TAG = _fast
else
    SOURCES = $(SOURCES_STD)
    VARIANT_TAG =
endif

# Compiler flags for RV32IMC (32 registers with MUL/DIV/barrel shifter + compressed instructions)
# ARCH and ABI are now loaded from ../config.mk (which parses .config)
CFLAGS = -march=$(ARCH) -mabi=$(ABI) -O2 -g
CFLAGS += -nostartfiles -nostdlib -nodefaultlibs
CFLAGS += -Wall -Wextra
CFLAGS += -ffreestanding -fno-builtin

# Linker flags
LDFLAGS = -T linker.ld -nostdlib -nostartfiles
LDFLAGS += -Wl,--gc-sections
LDFLAGS += -Wl,-Map=bootloader.map

# Output files (variant-specific)
ELF = bootloader$(VARIANT_TAG).elf
BIN = bootloader$(VARIANT_TAG).bin
HEX_VARIANT = bootloader$(VARIANT_TAG).hex
LST = bootloader$(VARIANT_TAG).lst
MAP = bootloader$(VARIANT_TAG).map

.PHONY: all clean size disasm std fast both set-std set-fast

# Default: build current variant and create/update bootloader.hex
all: $(BIN) $(HEX_VARIANT) $(LST) size update-symlink

# Update bootloader.hex to point to current variant
update-symlink:
	@if [ "$(VARIANT_TAG)" = "_fast" ]; then \
		echo "Creating symlink: bootloader.hex -> bootloader_fast.hex"; \
		rm -f bootloader.hex; \
		ln -s bootloader_fast.hex bootloader.hex; \
	fi
	@echo ""
	@echo "✓ Active bootloader: $(BOOTLOADER) (uses bootloader$(VARIANT_TAG).hex)"
	@echo ""

# Build standard bootloader
std:
	@$(MAKE) BOOTLOADER=std all

# Build fast bootloader
fast:
	@$(MAKE) BOOTLOADER=fast all

# Build both variants
both: std fast
	@echo ""
	@echo "========================================="
	@echo "Both bootloader variants built:"
	@echo "  Standard: bootloader.hex (chunked protocol)"
	@echo "  Fast:     bootloader_fast.hex (streaming protocol)"
	@echo "========================================="
	@echo ""

# Set standard as active (bootloader.hex becomes the real file, not a symlink)
set-std:
	@if [ ! -f bootloader.hex ] || [ -L bootloader.hex ]; then \
		if [ ! -f bootloader.hex ]; then \
			echo "Error: Standard bootloader.hex not found. Run 'make std' first."; \
			exit 1; \
		fi; \
		echo "Removing symlink and using standard bootloader.hex"; \
		rm -f bootloader.hex; \
		echo "Error: Please run 'make std' to rebuild standard bootloader"; \
		exit 1; \
	fi
	@echo "✓ Active bootloader: STANDARD (chunked protocol, bootloader.hex)"

# Set fast as active (bootloader.hex becomes a symlink)
set-fast:
	@if [ ! -f bootloader_fast.hex ]; then \
		echo "Error: bootloader_fast.hex not found. Run 'make fast' first."; \
		exit 1; \
	fi
	@echo "Creating symlink: bootloader.hex -> bootloader_fast.hex"
	@rm -f bootloader.hex
	@ln -s bootloader_fast.hex bootloader.hex
	@echo "✓ Active bootloader: FAST (streaming protocol, bootloader_fast.hex)"

# Link ELF
$(ELF): $(SOURCES) $(ASM_SOURCES) linker.ld
	$(CC) $(CFLAGS) $(LDFLAGS) $(ASM_SOURCES) $(SOURCES) -o $@

# Create binary
$(BIN): $(ELF)
	$(OBJCOPY) -O binary $< $@
	@echo "Bootloader binary size:"
	@ls -lh $@

# Create hex dump (32-bit words, little-endian for Verilog $readmemh)
$(HEX_VARIANT): $(BIN)
	@echo "@00000000" > $@
	@hexdump -v -e '1/4 "%08x\n"' $(BIN) >> $@
	@echo "Bootloader hex file created: $@"

# Disassembly listing (ALWAYS GENERATED)
$(LST): $(ELF)
	$(OBJDUMP) -D -S $< > $@
	@echo "========================================="
	@echo "Disassembly listing: $@"
	@echo "========================================="

# Show memory usage
size: $(ELF)
	@echo "========================================="
	@echo "Bootloader memory usage:"
	@echo "========================================="
	$(SIZE) $<
	@echo ""
	@echo "Memory layout:"
	@echo "  Code space:     0x00000000 - 0x0003FFFF (256KB)"
	@echo "  Bootloader ROM: 0x00040000 - 0x00041FFF (8KB)"
	@echo "  Stack:          0x00041F00 - 0x00041FFF (256B)"
	@echo "  Heap/Stack:     0x00042000 - 0x0007FFFF (~248KB)"
	@echo ""

# Disassemble (view listing)
disasm: $(LST)
	@cat $(LST) | less

# Clean build artifacts
clean:
	@echo "Cleaning bootloader build artifacts..."
	@rm -f bootloader.elf bootloader_fast.elf
	@rm -f bootloader.bin bootloader_fast.bin
	@rm -f bootloader.hex bootloader_fast.hex
	@rm -f bootloader.lst bootloader_fast.lst
	@rm -f bootloader.map bootloader_fast.map
	@echo "✓ Clean complete"

# Help
help:
	@echo "Bootloader Build Targets:"
	@echo "  make all       - Build current variant (default: standard)"
	@echo "  make std       - Build standard bootloader (chunked protocol)"
	@echo "  make fast      - Build FAST bootloader (streaming protocol)"
	@echo "  make both      - Build both variants"
	@echo "  make set-std   - Set standard as active (update bootloader.hex symlink)"
	@echo "  make set-fast  - Set fast as active (update bootloader.hex symlink)"
	@echo "  make size      - Show memory usage"
	@echo "  make disasm    - View disassembly listing"
	@echo "  make clean     - Remove build artifacts"
	@echo ""
	@echo "Bootloader Variants:"
	@echo "  Standard (bootloader.c)      - Chunked protocol with per-chunk ACKs"
	@echo "                                 Compatible with fw_upload"
	@echo "  Fast (bootloader_fast.c)     - Streaming protocol, NO chunking"
	@echo "                                 Compatible with fw_upload_fast ONLY"
	@echo ""
	@echo "Output files:"
	@echo "  bootloader.hex       - Active variant (symlink)"
	@echo "  bootloader.elf       - Standard ELF executable"
	@echo "  bootloader_fast.elf  - Fast ELF executable"
	@echo "  bootloader*.bin      - Raw binaries"
	@echo "  bootloader*.hex      - Verilog hex format (for BRAM init)"
	@echo "  bootloader*.lst      - Disassembly listings"
	@echo "  bootloader*.map      - Linker maps"
