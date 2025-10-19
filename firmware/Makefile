#===============================================================================
# Olimex iCE40HX8K-EVB RISC-V Platform
# Makefile - Firmware Build System with Newlib Support
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

# Newlib paths (installed in build/sysroot)
NEWLIB_INSTALL = ../build/sysroot
SYSCALLS_SRC = ../lib/syscalls.c
SYSCALLS_OBJ = syscalls.o

# Target firmware (override with TARGET=name)
TARGET ?= led_blink
SOURCES = $(TARGET).c

# Assembly startup code (use FreeRTOS version when USE_FREERTOS=1)
ifeq ($(USE_FREERTOS),1)
    ASM_SOURCES = startFRT.S
else
    ASM_SOURCES = start.S
endif

# Simple Upload library paths
SIMPLE_UPLOAD_DIR = ../lib/simple_upload
SIMPLE_UPLOAD_SRC = $(SIMPLE_UPLOAD_DIR)/simple_upload.c
SIMPLE_UPLOAD_OBJ = simple_upload.o

# MicroRL library paths
MICRORL_DIR = ../lib/microrl
MICRORL_SRC = $(MICRORL_DIR)/microrl.c
MICRORL_OBJ = microrl.o

# Incurses library paths
INCURSES_DIR = ../lib/incurses
INCURSES_SRC = $(INCURSES_DIR)/incurses.c
INCURSES_OBJ = incurses.o

# Use newlib flag (set USE_NEWLIB=1 to link with newlib)
USE_NEWLIB ?= 0

# Use FreeRTOS flag (set USE_FREERTOS=1 to link with FreeRTOS)
USE_FREERTOS ?= 0

# All firmware targets
FIRMWARE_TARGETS = led_blink interactive button_demo timer_clock
NEWLIB_TARGETS = printf_test uart_echo_test heap_test math_test algo_test mandelbrot_float mandelbrot_fixed

# Compiler flags for RV32IM
ARCH = rv32im
ABI = ilp32
CFLAGS = -march=$(ARCH) -mabi=$(ABI) -O2 -g
CFLAGS += -Wall -Wextra
CFLAGS += -ffreestanding -fno-builtin

# Hexedit uses microRL, Simple Upload, and incurses
ifeq ($(TARGET),hexedit)
    CFLAGS += -I$(MICRORL_DIR) -I$(SIMPLE_UPLOAD_DIR) -I$(INCURSES_DIR)
endif

# Hexedit_fast uses microRL and incurses (NO simple_upload library)
ifeq ($(TARGET),hexedit_fast)
    CFLAGS += -I$(MICRORL_DIR) -I$(INCURSES_DIR)
endif

# Mandelbrot_float uses incurses and timer (floating-point version)
ifeq ($(TARGET),mandelbrot_float)
    CFLAGS += -I$(INCURSES_DIR)
    SOURCES = mandelbrot_float.c timer_ms.c
endif

# Mandelbrot_fixed uses incurses and timer (optimized fixed-point version)
ifeq ($(TARGET),mandelbrot_fixed)
    CFLAGS += -I$(INCURSES_DIR)
    SOURCES = mandelbrot_fixed.c timer_ms.c
endif

# Conditional flags based on newlib usage
ifeq ($(USE_NEWLIB),1)
    # With newlib - STATICALLY LINKED for embedded system
    # Use our own start.S instead of crt0.o
    CFLAGS += -nostartfiles
    CFLAGS += -isystem $(NEWLIB_INSTALL)/riscv64-unknown-elf/include
    LDFLAGS = -T linker.ld -static -nostartfiles
    LDFLAGS += -L$(NEWLIB_INSTALL)/riscv64-unknown-elf/lib
    LDFLAGS += -Wl,--gc-sections
    LDFLAGS += -Wl,-Map=$(TARGET).map
    LIBS = $(SYSCALLS_OBJ) -lc -lm -lgcc
    $(info Building WITH newlib support (STATIC))
else
    # Without newlib - bare metal
    CFLAGS += -nostartfiles -nostdlib -nodefaultlibs
    LDFLAGS = -T linker.ld -nostdlib -nostartfiles
    LDFLAGS += -Wl,--gc-sections
    LDFLAGS += -Wl,-Map=$(TARGET).map
    LIBS = -lgcc
    $(info Building WITHOUT newlib (bare metal))
endif

# ============================================================================
# FreeRTOS RTOS Support
# ============================================================================
ifeq ($(USE_FREERTOS),1)
    # Force newlib (FreeRTOS requires C library)
    USE_NEWLIB = 1

    # FreeRTOS paths
    FREERTOS_DIR = ../downloads/freertos
    FREERTOS_PORT = ../lib/freertos_port
    FREERTOS_CONFIG = ../lib/freertos_config

    # Add FreeRTOS includes
    CFLAGS += -I$(FREERTOS_DIR)/include
    CFLAGS += -I$(FREERTOS_PORT)
    CFLAGS += -I$(FREERTOS_CONFIG)

    # Read CONFIG_* values from .config and pass as -D flags
    # Source .config to get variables, then add them as compiler flags
    include ../.config

    CFLAGS += -DCONFIG_FREERTOS_CPU_CLOCK_HZ=$(CONFIG_FREERTOS_CPU_CLOCK_HZ)
    CFLAGS += -DCONFIG_FREERTOS_TICK_RATE_HZ=$(CONFIG_FREERTOS_TICK_RATE_HZ)
    CFLAGS += -DCONFIG_FREERTOS_MAX_PRIORITIES=$(CONFIG_FREERTOS_MAX_PRIORITIES)
    CFLAGS += -DCONFIG_FREERTOS_MINIMAL_STACK_SIZE=$(CONFIG_FREERTOS_MINIMAL_STACK_SIZE)
    CFLAGS += -DCONFIG_FREERTOS_TOTAL_HEAP_SIZE=$(CONFIG_FREERTOS_TOTAL_HEAP_SIZE)

    # FreeRTOS optional features (from Kconfig .config)
    ifdef CONFIG_FREERTOS_INCLUDE_vTaskDelay
    CFLAGS += -DCONFIG_FREERTOS_INCLUDE_vTaskDelay
    endif
    ifdef CONFIG_FREERTOS_INCLUDE_vTaskDelayUntil
    CFLAGS += -DCONFIG_FREERTOS_INCLUDE_vTaskDelayUntil
    endif
    ifdef CONFIG_FREERTOS_INCLUDE_vTaskDelete
    CFLAGS += -DCONFIG_FREERTOS_INCLUDE_vTaskDelete
    endif
    ifdef CONFIG_FREERTOS_INCLUDE_xTaskGetCurrentTaskHandle
    CFLAGS += -DCONFIG_FREERTOS_INCLUDE_xTaskGetCurrentTaskHandle
    endif
    ifdef CONFIG_FREERTOS_INCLUDE_uxTaskPriorityGet
    CFLAGS += -DCONFIG_FREERTOS_INCLUDE_uxTaskPriorityGet
    endif
    ifdef CONFIG_FREERTOS_INCLUDE_uxTaskGetStackHighWaterMark
    CFLAGS += -DCONFIG_FREERTOS_INCLUDE_uxTaskGetStackHighWaterMark
    endif

    # FreeRTOS kernel sources
    FREERTOS_SRCS = \
        $(FREERTOS_DIR)/tasks.c \
        $(FREERTOS_DIR)/queue.c \
        $(FREERTOS_DIR)/list.c \
        $(FREERTOS_DIR)/timers.c \
        $(FREERTOS_DIR)/portable/MemMang/heap_4.c \
        $(FREERTOS_PORT)/port.c \
        $(FREERTOS_PORT)/freertos_irq.c

    # Compile to objects
    FREERTOS_OBJS = $(FREERTOS_SRCS:.c=.o)

    # Add to link (prepend before other libs)
    LIBS := $(FREERTOS_OBJS) $(LIBS)

    $(info Building WITH FreeRTOS RTOS support)
endif

# Add Simple Upload and incurses objects for hexedit
ifeq ($(TARGET),hexedit)
    LIBS := $(SIMPLE_UPLOAD_OBJ) $(INCURSES_OBJ) $(LIBS)
    $(info Building hexedit with Simple Upload and incurses support)
endif

# Add incurses and microRL objects for hexedit_fast (NO simple_upload)
ifeq ($(TARGET),hexedit_fast)
    LIBS := $(INCURSES_OBJ) $(LIBS)
    $(info Building hexedit_fast with FAST streaming protocol - NO chunking)
endif

# Add incurses object for mandelbrot_float
ifeq ($(TARGET),mandelbrot_float)
    LIBS := $(INCURSES_OBJ) $(LIBS)
    $(info Building mandelbrot_float (FLOATING-POINT) with incurses support)
endif

# Add incurses object for mandelbrot_fixed
ifeq ($(TARGET),mandelbrot_fixed)
    LIBS := $(INCURSES_OBJ) $(LIBS)
    $(info Building mandelbrot_fixed (FIXED-POINT) with incurses support)
endif

# Output files
ELF = $(TARGET).elf
BIN = $(TARGET).bin
HEX = $(TARGET).hex
LST = $(TARGET).lst
MAP = $(TARGET).map

.PHONY: all clean size disasm all-targets all-newlib-targets newlib-targets help build-newlib install-newlib
.PHONY: freertos_minimal freertos_demo

# Default: build all firmware (bare-metal + newlib + hexedit)
all: all-targets all-newlib-targets hexedit

# Build all firmware targets
all-targets:
	@echo "========================================="
	@echo "Building all firmware targets..."
	@echo "========================================="
	@for target in $(FIRMWARE_TARGETS); do \
		echo ""; \
		echo "Building $$target (bare metal)..."; \
		$(MAKE) TARGET=$$target USE_NEWLIB=0 single-target || exit 1; \
	done
	@echo ""
	@echo "========================================="
	@echo "All bare-metal firmware built successfully!"
	@echo "========================================="
	@ls -lh *.bin

# Build hexedit with Simple Upload and microRL
hexedit:
	@echo "========================================="
	@echo "Building hexedit with Simple Upload..."
	@echo "========================================="
	$(MAKE) TARGET=hexedit USE_NEWLIB=1 single-target
	@echo "✓ hexedit built successfully"

# Build hexedit_fast with FAST streaming protocol (NO chunking)
hexedit_fast:
	@echo "========================================="
	@echo "Building hexedit_fast with FAST streaming..."
	@echo "========================================="
	$(MAKE) TARGET=hexedit_fast USE_NEWLIB=1 single-target
	@echo "✓ hexedit_fast built successfully"

# Build FreeRTOS minimal test
freertos_minimal:
	@echo "========================================="
	@echo "Building FreeRTOS minimal test..."
	@echo "========================================="
	$(MAKE) TARGET=freertos_minimal USE_FREERTOS=1 USE_NEWLIB=1 single-target
	@echo "✓ freertos_minimal built successfully"

# Build FreeRTOS demo
freertos_demo:
	@echo "========================================="
	@echo "Building FreeRTOS demo..."
	@echo "========================================="
	$(MAKE) TARGET=freertos_demo USE_FREERTOS=1 USE_NEWLIB=1 single-target
	@echo "✓ freertos_demo built successfully"

# Build FreeRTOS printf demo (uses newlib printf)
freertos_printf_demo:
	@echo "========================================="
	@echo "Building FreeRTOS printf demo..."
	@echo "========================================="
	$(MAKE) TARGET=freertos_printf_demo USE_FREERTOS=1 USE_NEWLIB=1 single-target
	@echo "✓ freertos_printf_demo built successfully"

# Build newlib targets if newlib is installed
all-newlib-targets:
	@echo ""
	@if [ -d "$(NEWLIB_INSTALL)" ]; then \
		echo "=========================================" ; \
		echo "Building newlib-based targets..." ; \
		echo "=========================================" ; \
		for target in $(NEWLIB_TARGETS); do \
			echo "" ; \
			echo "Building $$target (with newlib)..." ; \
			$(MAKE) TARGET=$$target USE_NEWLIB=1 single-target || exit 1; \
		done ; \
		echo "" ; \
		echo "=========================================" ; \
		echo "All newlib targets built successfully!" ; \
		echo "=========================================" ; \
		ls -lh printf_test.bin 2>/dev/null || true ; \
	else \
		echo "=========================================" ; \
		echo "Newlib not installed - skipping newlib targets" ; \
		echo "Run 'make newlib-install' from top-level to enable" ; \
		echo "=========================================" ; \
	fi

# Build all newlib-based targets
newlib-targets: check-newlib
	@echo "========================================="
	@echo "Building newlib-based targets..."
	@echo "========================================="
	@for target in $(NEWLIB_TARGETS); do \
		echo ""; \
		echo "Building $$target (with newlib)..."; \
		$(MAKE) TARGET=$$target USE_NEWLIB=1 single-target || exit 1; \
	done
	@echo ""
	@echo "========================================="
	@echo "All newlib targets built successfully!"
	@echo "========================================="
	@ls -lh printf_test.bin

# Build single target
single-target: $(BIN) $(LST) size

# Compile syscalls (needed for newlib)
$(SYSCALLS_OBJ): ../lib/syscalls.c
	$(CC) $(CFLAGS) -c $< -o $@

# Compile Simple Upload library (needed for hexedit)
$(SIMPLE_UPLOAD_OBJ): $(SIMPLE_UPLOAD_SRC)
	$(CC) $(CFLAGS) -c $< -o $@

# Compile MicroRL library (needed for hexedit)
$(MICRORL_OBJ): $(MICRORL_SRC)
	$(CC) $(CFLAGS) -c $< -o $@

# Compile incurses library (needed for hexedit)
$(INCURSES_OBJ): $(INCURSES_SRC)
	$(CC) $(CFLAGS) -c $< -o $@

# Compile FreeRTOS sources
ifeq ($(USE_FREERTOS),1)
# Pattern rule for FreeRTOS kernel sources
$(FREERTOS_DIR)/%.o: $(FREERTOS_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Pattern rule for FreeRTOS port sources
$(FREERTOS_PORT)/%.o: $(FREERTOS_PORT)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Specific rule for heap_4.c (in subdirectory)
$(FREERTOS_DIR)/portable/MemMang/heap_4.o: $(FREERTOS_DIR)/portable/MemMang/heap_4.c
	$(CC) $(CFLAGS) -c $< -o $@
endif

# Link ELF
$(ELF): $(SOURCES) $(ASM_SOURCES) linker.ld
ifeq ($(USE_FREERTOS),1)
	@echo "Compiling FreeRTOS kernel sources..."
	@for obj in $(FREERTOS_OBJS); do \
		$(MAKE) $$obj || exit 1; \
	done
endif
ifeq ($(TARGET),hexedit)
	$(MAKE) $(SIMPLE_UPLOAD_OBJ)
	$(MAKE) $(MICRORL_OBJ)
	$(MAKE) $(INCURSES_OBJ)
endif
ifeq ($(TARGET),hexedit_fast)
	$(MAKE) $(MICRORL_OBJ)
	$(MAKE) $(INCURSES_OBJ)
endif
ifeq ($(TARGET),mandelbrot_float)
	$(MAKE) $(INCURSES_OBJ)
endif
ifeq ($(TARGET),mandelbrot_fixed)
	$(MAKE) $(INCURSES_OBJ)
endif
ifeq ($(USE_NEWLIB),1)
	$(MAKE) $(SYSCALLS_OBJ)
ifeq ($(TARGET),hexedit)
	$(CC) $(CFLAGS) $(LDFLAGS) $(ASM_SOURCES) $(SOURCES) $(MICRORL_OBJ) $(LIBS) -o $@
else ifeq ($(TARGET),hexedit_fast)
	$(CC) $(CFLAGS) $(LDFLAGS) $(ASM_SOURCES) $(SOURCES) $(MICRORL_OBJ) $(LIBS) -o $@
else ifeq ($(TARGET),tetris)
	$(CC) $(CFLAGS) $(LDFLAGS) $(ASM_SOURCES) $(SOURCES) $(LIBS) -o $@
else ifeq ($(TARGET),incurses_test)
	$(CC) $(CFLAGS) $(LDFLAGS) $(ASM_SOURCES) $(SOURCES) $(LIBS) -o $@
else
	$(CC) $(CFLAGS) $(LDFLAGS) $(ASM_SOURCES) $(SOURCES) $(LIBS) -o $@
endif
else
	$(CC) $(CFLAGS) $(LDFLAGS) $(ASM_SOURCES) $(SOURCES) $(LIBS) -o $@
endif

# Create binary
$(BIN): $(ELF)
	$(OBJCOPY) -O binary $< $@
	@echo "Binary size:"
	@ls -lh $@

# Create hex dump
$(HEX): $(ELF)
	$(OBJCOPY) -O verilog $< $@

# Disassembly listing (ALWAYS GENERATED)
$(LST): $(ELF)
	$(OBJDUMP) -D -S $< > $@
	@echo "Generated disassembly: $@"

# Show memory usage
size: $(ELF)
	@echo "===================================="
	@echo "Memory usage:"
	@echo "===================================="
	$(SIZE) $<
	@echo ""
	@echo "Memory layout (from linker script):"
	@echo "  Code/Data/BSS: 0x00000000 - 0x0003FFFF (256KB)"
	@echo "  Heap:          0x00042000 - 0x00080000 (248KB)"
	@echo "  Stack:         Grows down from 0x80000"

# Disassemble
disasm: $(LST)
	@cat $(LST)

# Check if newlib is installed
check-newlib:
	@if [ ! -d "$(NEWLIB_INSTALL)/riscv64-unknown-elf/lib/rv32im" ]; then \
		echo "ERROR: Newlib not found!"; \
		echo "Please run: make install-newlib"; \
		exit 1; \
	fi
	@echo "✓ Newlib installation found"

# Build newlib from source
build-newlib:
	@echo "========================================="
	@echo "Building newlib (this takes 30+ minutes)"
	@echo "========================================="
	@mkdir -p $(NEWLIB_BUILD)
	@cd $(NEWLIB_BUILD) && \
	if [ ! -f Makefile ]; then \
		$(NEWLIB_SRC)/configure \
			--target=riscv64-unknown-elf \
			--prefix=$(shell cd $(NEWLIB_INSTALL) && pwd) \
			--enable-newlib-nano-malloc \
			--enable-newlib-nano-formatted-io \
			--enable-newlib-io-float \
			--disable-newlib-supplied-syscalls \
			CFLAGS_FOR_TARGET="-march=$(ARCH) -mabi=$(ABI) -O2 -g"; \
	fi
	@cd $(NEWLIB_BUILD) && $(MAKE) -j4
	@echo "✓ Newlib build complete"

# Install newlib
install-newlib: build-newlib
	@echo "Installing newlib to $(NEWLIB_INSTALL)..."
	@cd $(NEWLIB_BUILD) && $(MAKE) install
	@echo "✓ Newlib installed successfully"
	@echo ""
	@echo "You can now build with: make TARGET=printf_test USE_NEWLIB=1"

# Clean build artifacts
clean:
	@echo "Cleaning all firmware build artifacts..."
	@rm -f *.elf *.bin *.hex *.lst *.map *.o
	@echo "✓ Clean complete"

# Clean newlib build
clean-newlib:
	@echo "Cleaning newlib build directory..."
	@rm -rf $(NEWLIB_BUILD)/*
	@echo "✓ Newlib build directory cleaned"

# Help
help:
	@echo "Firmware Build System with Newlib Support"
	@echo ""
	@echo "Basic Targets:"
	@echo "  make all                 - Build all bare-metal firmware"
	@echo "  make newlib-targets      - Build all newlib-based firmware"
	@echo "  make TARGET=name         - Build specific target (bare metal)"
	@echo "  make TARGET=name USE_NEWLIB=1 - Build with newlib"
	@echo ""
	@echo "Newlib Management:"
	@echo "  make build-newlib        - Build newlib from source (~30 min)"
	@echo "  make install-newlib      - Build and install newlib"
	@echo "  make check-newlib        - Check if newlib is installed"
	@echo "  make clean-newlib        - Clean newlib build directory"
	@echo ""
	@echo "Available Targets:"
	@echo "  Bare Metal:"
	@echo "    led_blink              - LED animation demo"
	@echo "    interactive            - UART echo server"
	@echo "    button_demo            - Button polling example"
	@echo "    timer_clock            - Real-time clock demo"
	@echo "  With Newlib:"
	@echo "    printf_test            - Full printf/scanf test"
	@echo ""
	@echo "Utility Targets:"
	@echo "  make size                - Show memory usage"
	@echo "  make disasm              - Show disassembly"
	@echo "  make clean               - Remove build artifacts"
