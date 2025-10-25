#===============================================================================
# SD Card + FatFS Manager - Local Makefile
#
# This Makefile downloads FatFS library and compiles the SD card manager
# application with all dependencies.
#
# Copyright (c) October 2025 Michael Wolak
# Email: mikewolak@gmail.com, mike@epromfoundry.com
#===============================================================================

# FatFS version and download info
FATFS_VERSION = R0.15
FATFS_URL = http://elm-chan.org/fsw/ff/arc/ff15.zip
FATFS_DIR = fatfs
FATFS_ZIP = ff15.zip

# Source files for this project
PROJECT_SOURCES = sd_card_manager.c sd_spi.c diskio.c io.c help.c

# FatFS source files we need
FATFS_SOURCES = $(FATFS_DIR)/source/ff.c

# All sources combined
SOURCES = $(PROJECT_SOURCES) $(FATFS_SOURCES)

# Object files
OBJS = $(SOURCES:.c=.o)

# Include paths
INCLUDES = -I. -I$(FATFS_DIR)/source

# This gets called by parent Makefile
# We just need to provide object files
all: check-fatfs $(OBJS)
	@echo "✓ SD/FatFS objects built"

# Download and extract FatFS if not present
check-fatfs:
	@if [ ! -d "$(FATFS_DIR)" ]; then \
		echo "========================================="; \
		echo "Downloading FatFS $(FATFS_VERSION)..."; \
		echo "========================================="; \
		wget -q $(FATFS_URL) -O $(FATFS_ZIP) || curl -sL $(FATFS_URL) -o $(FATFS_ZIP); \
		unzip -q $(FATFS_ZIP) -d $(FATFS_DIR); \
		rm -f $(FATFS_ZIP); \
		echo "✓ FatFS downloaded and extracted"; \
	fi

# Pattern rule for compiling C files
%.o: %.c
	@echo "  CC (SD/FatFS) $<"
	@$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Clean local objects
clean:
	@echo "Cleaning SD/FatFS objects..."
	@rm -f $(OBJS)
	@rm -f *.o
	@echo "✓ Clean complete"

# Clean everything including downloaded FatFS
distclean: clean
	@echo "Removing FatFS library..."
	@rm -rf $(FATFS_DIR)
	@echo "✓ FatFS removed (will re-download on next build)"

.PHONY: all check-fatfs clean distclean
