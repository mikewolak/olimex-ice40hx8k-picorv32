#===============================================================================
# Olimex iCE40HX8K-EVB RISC-V Platform
# Makefile - Uploader Build System
#
# Copyright (c) October 2025 Michael Wolak
# Email: mikewolak@gmail.com, mike@epromfoundry.com
#
# NOT FOR COMMERCIAL USE
# Educational and research purposes only
#===============================================================================


CC_MAC = clang
CC_LINUX = gcc
CC_WIN_GCC = x86_64-w64-mingw32-gcc
CC_WIN_VS = cl

CFLAGS_COMMON = -Wall -O2
CFLAGS_MAC = $(CFLAGS_COMMON) -framework CoreFoundation -framework IOKit
CFLAGS_LINUX = $(CFLAGS_COMMON)
CFLAGS_WIN_GCC = $(CFLAGS_COMMON) -lsetupapi
CFLAGS_WIN_VS = /O2 /W3

TARGET = fw_upload
TARGET_FAST = fw_upload_fast
SRC = fw_upload.c
SRC_FAST = fw_upload_fast.c

.PHONY: all mac linux win-gcc win-vs clean help fast

# Auto-detect platform and build both standard and fast versions
all:
	@if [ "$$(uname)" = "Linux" ]; then \
		$(MAKE) linux && $(MAKE) linux-fast; \
	elif [ "$$(uname)" = "Darwin" ]; then \
		$(MAKE) mac && $(MAKE) mac-fast; \
	else \
		echo "ERROR: Unsupported platform: $$(uname)"; \
		echo "Please specify target: make [mac|linux|win-gcc|win-vs]"; \
		echo "Run 'make help' for more information"; \
		exit 1; \
	fi

# macOS build
mac: $(SRC)
	@echo "Building for macOS..."
	$(CC_MAC) $(CFLAGS_MAC) -o $(TARGET) $(SRC)
	@echo "✓ Built: $(TARGET)"
	@echo ""
	@echo "Usage:"
	@echo "  ./$(TARGET) --list"
	@echo "  ./$(TARGET) -p /dev/cu.usbserial-XXXXX firmware.bin"

# macOS build (fast version)
mac-fast: $(SRC_FAST)
	@echo "Building FAST version for macOS..."
	$(CC_MAC) $(CFLAGS_MAC) -o $(TARGET_FAST) $(SRC_FAST)
	@echo "✓ Built: $(TARGET_FAST)"
	@echo ""
	@echo "Usage:"
	@echo "  ./$(TARGET_FAST) --list"
	@echo "  ./$(TARGET_FAST) -p /dev/cu.usbserial-XXXXX firmware.bin"

# Linux build
linux: $(SRC)
	@echo "Building for Linux..."
	$(CC_LINUX) $(CFLAGS_LINUX) -o $(TARGET) $(SRC)
	@echo "✓ Built: $(TARGET)"
	@echo ""
	@echo "Usage:"
	@echo "  ./$(TARGET) --list"
	@echo "  ./$(TARGET) -p /dev/ttyUSB0 firmware.bin"

# Linux build (fast version)
linux-fast: $(SRC_FAST)
	@echo "Building FAST version for Linux..."
	$(CC_LINUX) $(CFLAGS_LINUX) -o $(TARGET_FAST) $(SRC_FAST)
	@echo "✓ Built: $(TARGET_FAST)"
	@echo ""
	@echo "Usage:"
	@echo "  ./$(TARGET_FAST) --list"
	@echo "  ./$(TARGET_FAST) -p /dev/ttyUSB0 firmware.bin"

# Windows build with GCC/MinGW (cross-compile from Linux/Mac)
win-gcc: $(SRC)
	@echo "Building for Windows (MinGW)..."
	$(CC_WIN_GCC) $(CFLAGS_WIN_GCC) -o $(TARGET).exe $(SRC)
	@echo "✓ Built: $(TARGET).exe"
	@echo ""
	@echo "Usage (on Windows):"
	@echo "  $(TARGET).exe --list"
	@echo "  $(TARGET).exe -p COM8 firmware.bin"

# Windows build with Visual Studio (run on Windows)
win-vs: $(SRC)
	@echo "Building for Windows (Visual Studio)..."
	$(CC_WIN_VS) $(CFLAGS_WIN_VS) /Fe:$(TARGET).exe $(SRC)
	@echo "✓ Built: $(TARGET).exe"
	@echo ""
	@echo "Usage:"
	@echo "  $(TARGET).exe --list"
	@echo "  $(TARGET).exe -p COM8 firmware.bin"

# Clean build artifacts
clean:
	rm -f $(TARGET) $(TARGET_FAST) $(TARGET).exe $(TARGET_FAST).exe *.o *.obj *.pdb

# Install (Unix only)
install:
	@if [ -f $(TARGET) ]; then \
		echo "Installing to /usr/local/bin..."; \
		sudo cp $(TARGET) /usr/local/bin/; \
		echo "✓ Installed: /usr/local/bin/$(TARGET)"; \
	else \
		echo "ERROR: $(TARGET) not found. Run 'make mac' or 'make linux' first."; \
	fi

# Help
help:
	@echo "Firmware Uploader - Cross-platform Build System"
	@echo ""
	@echo "Targets:"
	@echo "  make mac        - Build for macOS (native)"
	@echo "  make linux      - Build for Linux (native)"
	@echo "  make win-gcc    - Build for Windows using MinGW (cross-compile)"
	@echo "  make win-vs     - Build for Windows using Visual Studio (on Windows)"
	@echo "  make clean      - Remove build artifacts"
	@echo "  make install    - Install to /usr/local/bin (Unix only)"
	@echo ""
	@echo "Features:"
	@echo "  - Native serial port control (termios on Unix, WinAPI on Windows)"
	@echo "  - Beautiful progress bar with real-time speed/ETA"
	@echo "  - Rotating ACK protocol with verbose mode"
	@echo "  - Cross-platform serial port listing (--list)"
	@echo ""
	@echo "Examples:"
	@echo "  # List available serial ports"
	@echo "  ./fw_upload --list"
	@echo ""
	@echo "  # Upload firmware (normal mode with progress bar)"
	@echo "  ./fw_upload -p /dev/cu.usbserial-XXXXX firmware.bin"
	@echo ""
	@echo "  # Upload with verbose output (show all ACKs)"
	@echo "  ./fw_upload -p /dev/cu.usbserial-XXXXX firmware.bin -v"
	@echo ""
	@echo "  # Custom baud rate"
	@echo "  ./fw_upload -p /dev/ttyUSB0 -b 57600 firmware.bin"
