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

# Use lwIP flag (set USE_LWIP=1 to link with lwIP TCP/IP stack)
USE_LWIP ?= 0

# All firmware targets organized by type
# Bare metal targets (no libraries)
BARE_METAL_TARGETS = led_blink interactive button_demo timer_clock coop_tasks irq_counter_test irq_timer_test softirq_test

# Newlib-only targets (requires newlib C library)
NEWLIB_TARGETS = printf_test uart_echo_test heap_test math_test algo_test stdio_test syscall_test interactive_test memory_test_baseline

# Incurses targets (requires newlib + incurses library)
INCURSES_TARGETS = mandelbrot_float mandelbrot_fixed spi_test

# Hexedit targets (requires newlib + microrl + incurses, hexedit also needs simple_upload)
HEXEDIT_TARGETS = hexedit hexedit_fast

# FreeRTOS targets (requires newlib + FreeRTOS)
FREERTOS_TARGETS = freertos_minimal freertos_demo freertos_printf_demo freertos_tasks_demo freertos_queue_demo

# FreeRTOS + incurses target
FREERTOS_INCURSES_TARGETS = freertos_curses_demo

# lwIP targets (requires newlib + lwIP stack)
LWIP_TARGETS = slip_echo_server slip_perf_server iperf_server tcp_perf_server slip_http_server

# SD/FatFS targets (requires newlib + incurses + FatFS)
SD_FATFS_TARGETS = sd_card_manager

# All firmware targets combined
ALL_FIRMWARE_TARGETS = $(BARE_METAL_TARGETS) $(NEWLIB_TARGETS) $(INCURSES_TARGETS) $(HEXEDIT_TARGETS) $(FREERTOS_TARGETS) $(FREERTOS_INCURSES_TARGETS) $(LWIP_TARGETS) $(SD_FATFS_TARGETS)

# Source files - lwIP demos are in subdirectory, SD/FatFS in sd_fatfs/, others are in root
# (Must be AFTER LWIP_TARGETS and SD_FATFS_TARGETS are defined)
# Only set if not already set by target-specific rules (e.g., mandelbrot targets with timer_ms.c)
ifndef SOURCE_FILE
ifeq ($(filter $(TARGET),$(LWIP_TARGETS)),$(TARGET))
    # lwIP targets: source is in lwIP/demos/ subdirectory
    SOURCE_FILE = lwIP/demos/$(TARGET).c
    SOURCES = $(TARGET).c
else ifeq ($(filter $(TARGET),$(SD_FATFS_TARGETS)),$(TARGET))
    # SD/FatFS targets: source is in sd_fatfs/ subdirectory
    SOURCE_FILE = sd_fatfs/$(TARGET).c
    SOURCES = $(TARGET).c
else
    # All other targets: source is in firmware/ root
    SOURCE_FILE = $(TARGET).c
    SOURCES = $(TARGET).c
endif
endif

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
    SOURCE_FILE = mandelbrot_float.c timer_ms.c
    SOURCES = mandelbrot_float.c timer_ms.c
endif

# Mandelbrot_fixed uses incurses and timer (optimized fixed-point version)
ifeq ($(TARGET),mandelbrot_fixed)
    CFLAGS += -I$(INCURSES_DIR)
    SOURCE_FILE = mandelbrot_fixed.c timer_ms.c
    SOURCES = mandelbrot_fixed.c timer_ms.c
endif

# SPI_test uses incurses library
ifeq ($(TARGET),spi_test)
    CFLAGS += -I$(INCURSES_DIR)
endif

# FreeRTOS curses demo uses incurses library
ifeq ($(TARGET),freertos_curses_demo)
    CFLAGS += -I$(INCURSES_DIR)
    SOURCES = freertos_curses_demo.c
endif

# SD Card Manager uses incurses and builds FatFS in sd_fatfs/ subdirectory
ifeq ($(TARGET),sd_card_manager)
    CFLAGS += -I$(INCURSES_DIR)
    USE_SD_FATFS = 1
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
    # Force inclusion of float formatting for printf/scanf
    LDFLAGS += -Wl,-u,_printf_float
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

    # Define USE_FREERTOS for conditional compilation
    CFLAGS += -DUSE_FREERTOS

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

# ============================================================================
# lwIP TCP/IP Stack Support (bare metal, NO_SYS mode)
# ============================================================================
ifeq ($(USE_LWIP),1)
    # lwIP requires newlib
    USE_NEWLIB = 1

    # Include lwIP build configuration
    include lwIP/port/lwip.mk

    # Add lwIP includes to CFLAGS
    CFLAGS += $(LWIP_INCLUDES)

    # Add lwIP objects to link (prepend before other libs)
    LIBS := $(LWIP_OBJS) $(LIBS)

    $(info Building WITH lwIP TCP/IP stack (NO_SYS mode))
endif

# SD/FatFS Configuration
ifeq ($(USE_SD_FATFS),1)
    # SD/FatFS requires newlib
    USE_NEWLIB = 1

    # Build SD/FatFS objects in subdirectory
    SD_FATFS_DIR = sd_fatfs

    # Export compiler flags to subdirectory make
    export CC
    export CFLAGS

    # Get list of object files from subdirectory
    SD_FATFS_OBJS = $(SD_FATFS_DIR)/sd_card_manager.o \
                     $(SD_FATFS_DIR)/sd_spi.o \
                     $(SD_FATFS_DIR)/diskio.o \
                     $(SD_FATFS_DIR)/io.o \
                     $(SD_FATFS_DIR)/help.o \
                     $(SD_FATFS_DIR)/overlay_upload.o \
                     $(SD_FATFS_DIR)/overlay_loader.o \
                     $(SD_FATFS_DIR)/file_browser.o \
                     $(SD_FATFS_DIR)/crash_dump.o \
                     $(SD_FATFS_DIR)/fatfs/source/ff.o \
                     $(SD_FATFS_DIR)/fatfs/source/ffunicode.o

    # Add SD/FatFS objects to link
    LIBS := $(SD_FATFS_OBJS) $(LIBS)

    $(info Building WITH SD/FatFS filesystem)
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

# Add incurses object for spi_test
ifeq ($(TARGET),spi_test)
    LIBS := $(INCURSES_OBJ) $(LIBS)
    $(info Building spi_test with incurses support)
endif

# Add incurses object for sd_card_manager (already has SD/FatFS objects)
ifeq ($(TARGET),sd_card_manager)
    LIBS := $(INCURSES_OBJ) $(LIBS)
endif

# Add incurses object for freertos_curses_demo
ifeq ($(TARGET),freertos_curses_demo)
    LIBS := $(INCURSES_OBJ) $(LIBS)
    $(info Building freertos_curses_demo with incurses and FreeRTOS support)
endif

# Output files
ELF = $(TARGET).elf
BIN = $(TARGET).bin
HEX = $(TARGET).hex
LST = $(TARGET).lst
MAP = $(TARGET).map

.PHONY: all firmware clean clean-lwip size disasm help build-newlib install-newlib check-newlib
.PHONY: bare-metal-targets newlib-only-targets incurses-targets hexedit-targets freertos-targets lwip-targets sd-fatfs-targets
.PHONY: $(HEXEDIT_TARGETS) $(FREERTOS_TARGETS) $(FREERTOS_INCURSES_TARGETS) $(LWIP_TARGETS) $(INCURSES_TARGETS) $(SD_FATFS_TARGETS)

# Default: just sd_card_manager (fast for quick testing)
all:
	@echo "Building default target: sd_card_manager (use 'make firmware' to build everything)"
	$(MAKE) sd_card_manager

# Build ALL firmware targets (this is what user expects)
firmware: bare-metal-targets newlib-only-targets incurses-targets hexedit-targets freertos-targets lwip-targets sd-fatfs-targets
	@echo ""
	@echo "========================================================================"
	@echo "ALL FIRMWARE BUILT SUCCESSFULLY!"
	@echo "========================================================================"
	@echo "Bare metal:  $(BARE_METAL_TARGETS)"
	@echo "Newlib:      $(NEWLIB_TARGETS)"
	@echo "Incurses:    $(INCURSES_TARGETS)"
	@echo "Hexedit:     $(HEXEDIT_TARGETS)"
	@echo "FreeRTOS:    $(FREERTOS_TARGETS) $(FREERTOS_INCURSES_TARGETS)"
	@echo "lwIP:        $(LWIP_TARGETS)"
	@echo "SD/FatFS:    $(SD_FATFS_TARGETS)"
	@echo "========================================================================"
	@ls -lh *.bin | tail -20

# Build bare metal targets (no libraries)
bare-metal-targets:
	@echo "========================================="
	@echo "Building bare metal targets..."
	@echo "========================================="
	@for target in $(BARE_METAL_TARGETS); do \
		echo ""; \
		echo "Building $$target (bare metal)..."; \
		$(MAKE) TARGET=$$target USE_NEWLIB=0 single-target || exit 1; \
	done
	@echo "✓ All bare-metal targets built"

# Build newlib-only targets
newlib-only-targets:
	@echo ""
	@if [ -d "$(NEWLIB_INSTALL)" ]; then \
		echo "=========================================" ; \
		echo "Building newlib targets..." ; \
		echo "=========================================" ; \
		for target in $(NEWLIB_TARGETS); do \
			echo "" ; \
			echo "Building $$target (with newlib)..." ; \
			$(MAKE) TARGET=$$target USE_NEWLIB=1 single-target || exit 1; \
		done ; \
		echo "✓ All newlib targets built" ; \
	else \
		echo "=========================================" ; \
		echo "SKIPPED: Newlib not installed" ; \
		echo "Run 'make install-newlib' from top-level to enable newlib targets" ; \
		echo "=========================================" ; \
	fi

# Build incurses targets (mandelbrot demos)
incurses-targets:
	@echo ""
	@if [ -d "$(NEWLIB_INSTALL)" ]; then \
		echo "=========================================" ; \
		echo "Building incurses targets..." ; \
		echo "=========================================" ; \
		for target in $(INCURSES_TARGETS); do \
			echo "" ; \
			echo "Building $$target (with incurses)..." ; \
			$(MAKE) TARGET=$$target USE_NEWLIB=1 single-target || exit 1; \
		done ; \
		echo "✓ All incurses targets built" ; \
	else \
		echo "=========================================" ; \
		echo "SKIPPED: Incurses targets need newlib" ; \
		echo "=========================================" ; \
	fi

# Build hexedit targets
hexedit-targets:
	@echo ""
	@if [ -d "$(NEWLIB_INSTALL)" ]; then \
		echo "=========================================" ; \
		echo "Building hexedit targets..." ; \
		echo "=========================================" ; \
		for target in $(HEXEDIT_TARGETS); do \
			echo "" ; \
			echo "Building $$target..." ; \
			$(MAKE) $$target || exit 1; \
		done ; \
		echo "✓ All hexedit targets built" ; \
	else \
		echo "=========================================" ; \
		echo "SKIPPED: Hexedit targets need newlib" ; \
		echo "=========================================" ; \
	fi

# Build FreeRTOS targets
freertos-targets:
	@echo ""
	@if [ -d "$(NEWLIB_INSTALL)" ]; then \
		echo "=========================================" ; \
		echo "Building FreeRTOS targets..." ; \
		echo "=========================================" ; \
		for target in $(FREERTOS_TARGETS); do \
			echo "" ; \
			echo "Building $$target (with FreeRTOS)..." ; \
			$(MAKE) $$target || exit 1; \
		done ; \
		for target in $(FREERTOS_INCURSES_TARGETS); do \
			echo "" ; \
			echo "Building $$target (with FreeRTOS + incurses)..." ; \
			$(MAKE) $$target || exit 1; \
		done ; \
		echo "✓ All FreeRTOS targets built" ; \
	else \
		echo "=========================================" ; \
		echo "SKIPPED: FreeRTOS targets need newlib" ; \
		echo "=========================================" ; \
	fi

# Build lwIP targets
lwip-targets:
	@echo ""
	@if [ -d "$(NEWLIB_INSTALL)" ]; then \
		echo "=========================================" ; \
		echo "Building lwIP targets..." ; \
		echo "=========================================" ; \
		for target in $(LWIP_TARGETS); do \
			echo "" ; \
			echo "Building $$target (with lwIP)..." ; \
			$(MAKE) $$target || exit 1; \
		done ; \
		echo "✓ All lwIP targets built" ; \
	else \
		echo "=========================================" ; \
		echo "SKIPPED: lwIP targets need newlib" ; \
		echo "=========================================" ; \
	fi

# Build SD/FatFS targets
sd-fatfs-targets:
	@echo ""
	@if [ -d "$(NEWLIB_INSTALL)" ]; then \
		echo "=========================================" ; \
		echo "Building SD/FatFS targets..." ; \
		echo "=========================================" ; \
		for target in $(SD_FATFS_TARGETS); do \
			echo "" ; \
			echo "Building $$target (with SD/FatFS)..." ; \
			$(MAKE) $$target || exit 1; \
		done ; \
		echo "✓ All SD/FatFS targets built" ; \
	else \
		echo "=========================================" ; \
		echo "SKIPPED: SD/FatFS targets need newlib" ; \
		echo "=========================================" ; \
	fi

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

# Build software IRQ / trap test (bare metal)
softirq_test:
	@echo "========================================="
	@echo "Building software IRQ test..."
	@echo "========================================="
	$(MAKE) TARGET=softirq_test USE_NEWLIB=0 single-target
	@echo "✓ softirq_test built successfully"

# Individual FreeRTOS build targets
freertos_minimal:
	$(MAKE) TARGET=freertos_minimal USE_FREERTOS=1 USE_NEWLIB=1 single-target

freertos_demo:
	$(MAKE) TARGET=freertos_demo USE_FREERTOS=1 USE_NEWLIB=1 single-target

freertos_printf_demo:
	$(MAKE) TARGET=freertos_printf_demo USE_FREERTOS=1 USE_NEWLIB=1 single-target

freertos_tasks_demo:
	$(MAKE) TARGET=freertos_tasks_demo USE_FREERTOS=1 USE_NEWLIB=1 single-target

freertos_queue_demo:
	$(MAKE) TARGET=freertos_queue_demo USE_FREERTOS=1 USE_NEWLIB=1 single-target

freertos_curses_demo:
	$(MAKE) TARGET=freertos_curses_demo USE_FREERTOS=1 USE_NEWLIB=1 single-target

# Individual lwIP build targets
slip_echo_server:
	$(MAKE) TARGET=slip_echo_server USE_LWIP=1 USE_NEWLIB=1 single-target

slip_perf_server:
	$(MAKE) TARGET=slip_perf_server USE_LWIP=1 USE_NEWLIB=1 single-target

iperf_server:
	$(MAKE) TARGET=iperf_server USE_LWIP=1 USE_NEWLIB=1 single-target

tcp_perf_server:
	$(MAKE) TARGET=tcp_perf_server USE_LWIP=1 USE_NEWLIB=1 single-target

slip_http_server:
	$(MAKE) TARGET=slip_http_server USE_LWIP=1 USE_NEWLIB=1 single-target

# Individual incurses build targets
spi_test:
	$(MAKE) TARGET=spi_test USE_NEWLIB=1 single-target

mandelbrot_float:
	$(MAKE) TARGET=mandelbrot_float USE_NEWLIB=1 single-target

mandelbrot_fixed:
	$(MAKE) TARGET=mandelbrot_fixed USE_NEWLIB=1 single-target

# Individual SD/FatFS build targets
sd_card_manager:
	$(MAKE) TARGET=sd_card_manager USE_SD_FATFS=1 USE_NEWLIB=1 single-target

# Build single target
single-target: $(BIN) $(LST) size

# Compile syscalls (needed for newlib)
# Force rebuild when switching between FreeRTOS and non-FreeRTOS builds
ifeq ($(USE_FREERTOS),1)
$(SYSCALLS_OBJ): ../lib/syscalls.c .syscalls_build_mode
	$(CC) $(CFLAGS) -c $< -o $@

.syscalls_build_mode:
	@if [ -f .syscalls_nofreertos ]; then \
		echo "Switching from non-FreeRTOS to FreeRTOS build - forcing syscalls.o rebuild"; \
		rm -f $(SYSCALLS_OBJ) .syscalls_nofreertos; \
	fi
	@touch .syscalls_freertos
else
$(SYSCALLS_OBJ): ../lib/syscalls.c .syscalls_build_mode
	$(CC) $(CFLAGS) -c $< -o $@

.syscalls_build_mode:
	@if [ -f .syscalls_freertos ]; then \
		echo "Switching from FreeRTOS to non-FreeRTOS build - forcing syscalls.o rebuild"; \
		rm -f $(SYSCALLS_OBJ) .syscalls_freertos; \
	fi
	@touch .syscalls_nofreertos
endif

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
# IMPORTANT: All FreeRTOS objects depend on ../.config to force rebuild when CONFIG_* changes
$(FREERTOS_DIR)/%.o: $(FREERTOS_DIR)/%.c ../.config
	$(CC) $(CFLAGS) -c $< -o $@

# Pattern rule for FreeRTOS port sources
$(FREERTOS_PORT)/%.o: $(FREERTOS_PORT)/%.c ../.config
	$(CC) $(CFLAGS) -c $< -o $@

# Specific rule for heap_4.c (in subdirectory)
$(FREERTOS_DIR)/portable/MemMang/heap_4.o: $(FREERTOS_DIR)/portable/MemMang/heap_4.c ../.config
	$(CC) $(CFLAGS) -c $< -o $@
endif

# Compile lwIP sources
ifeq ($(USE_LWIP),1)
# Pattern rules are defined in lwIP/port/lwip.mk
endif

# Link ELF
# Note: SOURCES is used in the compile command but not as a dependency for lwIP targets (they're in subdirectories)
$(ELF): $(ASM_SOURCES) linker.ld
ifeq ($(USE_LWIP),1)
	@echo "Compiling lwIP TCP/IP stack sources..."
	@for obj in $(LWIP_OBJS); do \
		$(MAKE) $$obj || exit 1; \
	done
endif
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
ifeq ($(TARGET),spi_test)
	$(MAKE) $(INCURSES_OBJ)
endif
ifeq ($(TARGET),freertos_curses_demo)
	$(MAKE) $(INCURSES_OBJ)
endif
ifeq ($(TARGET),sd_card_manager)
	$(MAKE) $(INCURSES_OBJ)
	@echo "Compiling SD/FatFS sources..."
	@$(MAKE) -C sd_fatfs all CC=$(CC) CFLAGS="$(subst ../build,../../build,$(CFLAGS))" || exit 1
endif
ifeq ($(USE_NEWLIB),1)
	$(MAKE) $(SYSCALLS_OBJ)
ifeq ($(TARGET),hexedit)
	$(CC) $(CFLAGS) $(LDFLAGS) $(ASM_SOURCES) $(SOURCE_FILE) $(MICRORL_OBJ) $(LIBS) -o $@
else ifeq ($(TARGET),hexedit_fast)
	$(CC) $(CFLAGS) $(LDFLAGS) $(ASM_SOURCES) $(SOURCE_FILE) $(MICRORL_OBJ) $(LIBS) -o $@
else ifeq ($(TARGET),tetris)
	$(CC) $(CFLAGS) $(LDFLAGS) $(ASM_SOURCES) $(SOURCE_FILE) $(LIBS) -o $@
else ifeq ($(TARGET),incurses_test)
	$(CC) $(CFLAGS) $(LDFLAGS) $(ASM_SOURCES) $(SOURCE_FILE) $(LIBS) -o $@
else ifeq ($(TARGET),sd_card_manager)
	$(CC) $(CFLAGS) $(LDFLAGS) $(ASM_SOURCES) $(LIBS) -o $@
else
	$(CC) $(CFLAGS) $(LDFLAGS) $(ASM_SOURCES) $(SOURCE_FILE) $(LIBS) -o $@
endif
else
	$(CC) $(CFLAGS) $(LDFLAGS) $(ASM_SOURCES) $(SOURCE_FILE) $(LIBS) -o $@
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
ifeq ($(USE_FREERTOS),1)
	@echo ""
	@bash ../scripts/validate_freertos_build.sh $< ../.config || (echo "BUILD VALIDATION FAILED!" && exit 1)
endif
ifeq ($(USE_LWIP),1)
	@echo ""
	@bash ../scripts/validate_lwip_build.sh $< lwIP/port/lwipopts.h || (echo "BUILD VALIDATION FAILED!" && exit 1)
endif

# Disassemble
disasm: $(LST)
	@cat $(LST)

# Check if newlib is installed
check-newlib:
	@if [ ! -d "$(NEWLIB_INSTALL)/riscv64-unknown-elf/lib/rv32im" ]; then \
		echo "⚠ Newlib not found at $(NEWLIB_INSTALL)"; \
		echo "  (Will auto-build if needed - use 'make newlib-if-needed' from top level)"; \
	else \
		echo "✓ Newlib installation found"; \
	fi

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

# Clean lwIP objects (required after lwipopts.h changes)
clean-lwip:
	@echo "Cleaning lwIP object files..."
	@find ../downloads/lwip -name "*.o" -type f -delete 2>/dev/null || true
	@find lwIP/port -name "*.o" -type f -delete 2>/dev/null || true
	@echo "✓ lwIP objects cleaned"
	@echo ""
	@echo "IMPORTANT: Rebuild your target after cleaning lwIP:"
	@echo "  make slip_echo_server    # or your specific target"
	@echo ""

# Help
help:
	@echo "========================================================================"
	@echo "Firmware Build System - Comprehensive Multi-Library Support"
	@echo "========================================================================"
	@echo ""
	@echo "Main Targets:"
	@echo "  make                     - Build led_blink (fast default)"
	@echo "  make firmware            - Build ALL firmware targets (this is what you want!)"
	@echo "  make TARGET=name         - Build specific target"
	@echo ""
	@echo "Organized Builds:"
	@echo "  make bare-metal-targets  - All bare metal firmware ($(words $(BARE_METAL_TARGETS)) targets)"
	@echo "  make newlib-only-targets - All newlib targets ($(words $(NEWLIB_TARGETS)) targets)"
	@echo "  make incurses-targets    - Mandelbrot demos ($(words $(INCURSES_TARGETS)) targets)"
	@echo "  make hexedit-targets     - Hexedit variants ($(words $(HEXEDIT_TARGETS)) targets)"
	@echo "  make freertos-targets    - FreeRTOS demos ($(words $(FREERTOS_TARGETS) $(FREERTOS_INCURSES_TARGETS)) targets)"
	@echo "  make lwip-targets        - lwIP/SLIP networking ($(words $(LWIP_TARGETS)) targets)"
	@echo ""
	@echo "Individual Targets:"
	@echo "  Bare Metal: $(BARE_METAL_TARGETS)"
	@echo "  Newlib:     $(NEWLIB_TARGETS)"
	@echo "  Incurses:   $(INCURSES_TARGETS)"
	@echo "  Hexedit:    $(HEXEDIT_TARGETS)"
	@echo "  FreeRTOS:   $(FREERTOS_TARGETS) $(FREERTOS_INCURSES_TARGETS)"
	@echo "  lwIP:       $(LWIP_TARGETS)"
	@echo ""
	@echo "Library Management:"
	@echo "  make check-newlib        - Check if newlib is installed"
	@echo "  make install-newlib      - Build and install newlib (~30 min, from top-level)"
	@echo ""
	@echo "Utility:"
	@echo "  make size                - Show memory usage"
	@echo "  make disasm              - Show disassembly"
	@echo "  make clean               - Remove build artifacts"
	@echo "  make clean-lwip          - Clean lwIP objects (after lwipopts.h changes)"
	@echo "========================================================================"
