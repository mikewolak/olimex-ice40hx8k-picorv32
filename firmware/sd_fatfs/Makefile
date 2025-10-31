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

# uzlib paths (decompression library)
UZLIB_DIR = ../../downloads/uzlib
UZLIB_SRC = $(UZLIB_DIR)/src

# Source files for this project
PROJECT_SOURCES = sd_card_manager.c sd_spi.c diskio.c io.c help.c overlay_upload.c overlay_loader.c file_browser.c crash_dump.c

# FatFS source files we need
FATFS_SOURCES = $(FATFS_DIR)/source/ff.c $(FATFS_DIR)/source/ffunicode.c

# uzlib source files (decompression only - no compression needed)
UZLIB_SOURCES = $(UZLIB_SRC)/tinflate.c $(UZLIB_SRC)/tinfgzip.c $(UZLIB_SRC)/adler32.c $(UZLIB_SRC)/crc32.c

# All sources combined
SOURCES = $(PROJECT_SOURCES) $(FATFS_SOURCES) $(UZLIB_SOURCES)

# Object files
OBJS = $(SOURCES:.c=.o)

# Include paths
INCLUDES = -I. -I$(FATFS_DIR)/source -I$(UZLIB_SRC)

# This gets called by parent Makefile
# We just need to provide object files
all: check-fatfs check-uzlib $(OBJS)
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

# Check if uzlib is present
check-uzlib:
	@if [ ! -d "$(UZLIB_DIR)" ]; then \
		echo "========================================="; \
		echo "ERROR: uzlib not found!"; \
		echo "========================================="; \
		echo "Please download uzlib with:"; \
		echo "  cd ../../downloads && git clone --depth 1 https://github.com/pfalcon/uzlib.git"; \
		echo "Or run: make uzlib-download from top-level Makefile"; \
		exit 1; \
	fi

# Pattern rules for compiling C files in different directories

# Local project files
%.o: %.c
	@echo "  CC (SD/FatFS) $<"
	@$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# FatFS library files
$(FATFS_DIR)/source/%.o: $(FATFS_DIR)/source/%.c
	@echo "  CC (FatFS) $<"
	@$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# uzlib library files
$(UZLIB_SRC)/%.o: $(UZLIB_SRC)/%.c
	@echo "  CC (uzlib) $<"
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

.PHONY: all check-fatfs check-uzlib clean distclean
