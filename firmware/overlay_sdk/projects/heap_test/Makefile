#===============================================================================
# Overlay Project: heap_test
# Makefile - Build script for heap_test overlay
#
# Copyright (c) October 2025
#===============================================================================

# Project name (will be replaced by actual name)
PROJECT_NAME = heap_test

#===============================================================================
# Include Overlay SDK Master Makefile
#===============================================================================

# This provides all the shared settings:
# - OVERLAY_CFLAGS (compiler flags with -fPIC)
# - OVERLAY_LDFLAGS (linker flags)
# - OVERLAY_START (startup code)
# - OVERLAY_IO (I/O helpers)
# - OVERLAY_LIBS (standard libraries)

include ../../Makefile.overlay

#===============================================================================
# Project-Specific Configuration
#===============================================================================

# Source files for this overlay
SOURCES = main.c

# Object files (generated from sources)
OBJECTS = $(SOURCES:.c=.o)

# Additional include paths (if needed)
# CFLAGS += -I./include

# Additional libraries (if needed)
# LIBS += -lmylib

#===============================================================================
# Build Targets
#===============================================================================

.PHONY: all clean size disasm help

# Default target: build the overlay binary
all: $(PROJECT_NAME).bin size

#-------------------------------------------------------------------------------
# Link overlay ELF executable
#-------------------------------------------------------------------------------

$(PROJECT_NAME).elf: $(OBJECTS) $(OVERLAY_START) $(OVERLAY_IO)
	@echo "========================================="
	@echo "Linking overlay: $(PROJECT_NAME).elf"
	@echo "========================================="
	$(CC) $(OVERLAY_CFLAGS) $(OVERLAY_LDFLAGS) \
		$(OVERLAY_START) \
		$(OVERLAY_IO) \
		$(OBJECTS) \
		$(OVERLAY_LIBS) \
		-o $@
	@echo "✓ Linking complete"
	@echo ""

#-------------------------------------------------------------------------------
# Create binary from ELF
#-------------------------------------------------------------------------------

$(PROJECT_NAME).bin: $(PROJECT_NAME).elf
	@echo "Creating binary: $(PROJECT_NAME).bin"
	@$(OBJCOPY) -O binary $< $@
	@echo "✓ Binary created:"
	@ls -lh $@
	@echo ""

#-------------------------------------------------------------------------------
# Show memory usage
#-------------------------------------------------------------------------------

size: $(PROJECT_NAME).elf
	@$(MAKE) -f ../../Makefile.overlay overlay-size PROJECT_NAME=$(PROJECT_NAME)

#-------------------------------------------------------------------------------
# Generate and view disassembly
#-------------------------------------------------------------------------------

disasm: $(PROJECT_NAME).lst
	@$(MAKE) -f ../../Makefile.overlay overlay-disasm PROJECT_NAME=$(PROJECT_NAME)

$(PROJECT_NAME).lst: $(PROJECT_NAME).elf
	@$(MAKE) -f ../../Makefile.overlay $@

#-------------------------------------------------------------------------------
# Clean build artifacts
#-------------------------------------------------------------------------------

clean:
	@$(MAKE) -f ../../Makefile.overlay overlay-clean PROJECT_NAME=$(PROJECT_NAME)

#-------------------------------------------------------------------------------
# Help
#-------------------------------------------------------------------------------

help:
	@echo "Overlay Project: $(PROJECT_NAME)"
	@echo ""
	@echo "Targets:"
	@echo "  make all      - Build overlay binary (default)"
	@echo "  make size     - Show memory usage"
	@echo "  make disasm   - View disassembly listing"
	@echo "  make clean    - Remove build artifacts"
	@echo "  make help     - Show this help"
	@echo ""
	@echo "Output files:"
	@echo "  $(PROJECT_NAME).elf  - Overlay executable (with debug info)"
	@echo "  $(PROJECT_NAME).bin  - Overlay binary (upload to SD card)"
	@echo "  $(PROJECT_NAME).lst  - Disassembly listing"
	@echo "  $(PROJECT_NAME).map  - Linker map file"
	@echo ""
	@echo "Upload to SD card:"
	@echo "  1. Load SD Card Manager on device"
	@echo "  2. Select 'Upload Overlay' from menu"
	@echo "  3. Upload $(PROJECT_NAME).bin"
	@echo ""
	@echo "Run overlay:"
	@echo "  1. Select 'Browse and Run Overlays' from menu"
	@echo "  2. Select $(PROJECT_NAME).BIN"
	@echo "  3. Overlay will execute and return to menu"
	@echo ""

#===============================================================================
# Dependencies
#===============================================================================

# Object files depend on headers
$(OBJECTS): $(OVERLAY_INCLUDES)
