#===============================================================================
# SD Bootloader Build System
#
# Builds the SD card bootloader that will be burned into the HDL bitstream.
# This bootloader runs from BRAM at 0x10000 and loads the main bootloader
# from SD card to SRAM at 0x0.
#
# Copyright (c) October 2025 Michael Wolak
# Email: mikewolak@gmail.com, mike@epromfoundry.com
#===============================================================================

# Detect host OS and set appropriate toolchain prefix
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    PREFIX = riscv-none-elf-
    $(info Building on macOS - using $(PREFIX) toolchain)
else
    PREFIX = riscv64-unknown-elf-
    $(info Building on $(UNAME_S) - using $(PREFIX) toolchain)
endif

CC = $(PREFIX)gcc
AS = $(PREFIX)as
LD = $(PREFIX)ld
OBJCOPY = $(PREFIX)objcopy
OBJDUMP = $(PREFIX)objdump
SIZE = $(PREFIX)size

# Target name
TARGET = sd_bootloader

# Source files
C_SOURCES = sd_bootloader.c sd_spi_minimal.c
ASM_SOURCES = start_bootloader.S

# Object files
C_OBJS = $(C_SOURCES:.c=.o)
ASM_OBJS = $(ASM_SOURCES:.S=.o)
OBJS = $(ASM_OBJS) $(C_OBJS)

# Linker script
LDSCRIPT = bootloader.ld

# Compiler flags
CFLAGS = -march=rv32im -mabi=ilp32
CFLAGS += -Os
CFLAGS += -ffunction-sections -fdata-sections
CFLAGS += -Wall -Wextra
CFLAGS += -ffreestanding -nostdlib
CFLAGS += -I.

# Assembler flags
ASFLAGS = -march=rv32im -mabi=ilp32

# Linker flags
LDFLAGS = -T$(LDSCRIPT)
LDFLAGS += -nostdlib
LDFLAGS += -Wl,--gc-sections
LDFLAGS += -Wl,-Map=$(TARGET).map

#===============================================================================
# Build Rules
#===============================================================================

.PHONY: all clean

all: $(TARGET).bin $(TARGET).hex

# Link to ELF
$(TARGET).elf: $(OBJS) $(LDSCRIPT)
	@echo "Linking $@..."
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) -o $@
	@echo ""
	@echo "=== Bootloader Size ==="
	$(SIZE) $@
	@echo ""

# Convert ELF to binary
$(TARGET).bin: $(TARGET).elf
	@echo "Creating binary $@..."
	$(OBJCOPY) -O binary $< $@
	@echo ""
	@echo "=== Binary Size ==="
	@ls -lh $@
	@echo ""

# Convert ELF to hex (for HDL synthesis)
# Format: 32-bit words, little-endian, matching UART bootloader format
$(TARGET).hex: $(TARGET).bin
	@echo "Creating hex file $@..."
	@echo "@00000000" > $@
	@hexdump -v -e '1/4 "%08x\n"' $< >> $@
	@echo "Hex file format: 32-bit words, little-endian (compatible with bootloader_rom.v)"
	@echo ""

# Compile C sources
%.o: %.c
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

# Assemble sources
%.o: %.S
	@echo "Assembling $<..."
	$(CC) $(ASFLAGS) -c $< -o $@

# Disassembly for debugging
$(TARGET).asm: $(TARGET).elf
	@echo "Creating disassembly $@..."
	$(OBJDUMP) -d $< > $@

disasm: $(TARGET).asm

# Clean build artifacts
clean:
	@echo "Cleaning bootloader build artifacts..."
	rm -f $(OBJS) $(TARGET).elf $(TARGET).bin $(TARGET).hex $(TARGET).map $(TARGET).asm
	@echo "Clean complete."

# Help
help:
	@echo "SD Bootloader Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all     - Build bootloader (default)"
	@echo "  clean   - Remove build artifacts"
	@echo "  disasm  - Create disassembly listing"
	@echo "  help    - Show this help"
	@echo ""
	@echo "Output files:"
	@echo "  $(TARGET).elf - Executable with debug symbols"
	@echo "  $(TARGET).bin - Raw binary for uploading"
	@echo "  $(TARGET).hex - Verilog hex format for HDL synthesis"
	@echo "  $(TARGET).map - Linker map file"
