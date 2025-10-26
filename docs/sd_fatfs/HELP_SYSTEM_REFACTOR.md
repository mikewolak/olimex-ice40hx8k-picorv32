# Help System Refactoring - SD Card Manager

## Overview

The interactive help system has been extracted into its own modular files for better code organization and maintainability.

## Changes Made

### Files Created

1. **help.h** - Header file with function prototype
   - Clean interface for the help system
   - Documents navigation controls

2. **help.c** - Implementation of the help system
   - Contains the complete 3-page interactive help
   - All help content and page rendering logic

### Files Modified

1. **sd_card_manager.c**
   - Removed `show_help()` function (now in help.c)
   - Added `#include "help.h"`
   - Main menu still calls `show_help()` via 'H' key

2. **Makefile** (sd_fatfs/Makefile)
   - Added `help.c` to `PROJECT_SOURCES`

3. **Makefile** (firmware/Makefile)
   - Added `$(SD_FATFS_DIR)/help.o` to `SD_FATFS_OBJS`

## Key Improvements

### 1. Infinite Page Looping (FIXED)

**Before**:
```c
if (ch == ' ' && page < max_page) {  // Stops at last page
    page++;
    need_redraw = 1;
}
```

**After**:
```c
if (ch == ' ') {  // Loops infinitely
    page = (page + 1) % (max_page + 1);
    need_redraw = 1;
}
```

- Pressing SPACE on page 3 now wraps back to page 1
- Works in both forward (SPACE) and backward (B) directions

### 2. Backward Navigation

Added modular arithmetic for backward looping:
```c
if (ch == 'b' || ch == 'B') {
    page = (page - 1 + (max_page + 1)) % (max_page + 1);
    need_redraw = 1;
}
```

- Pressing 'B' on page 1 now wraps to page 3
- Creates seamless circular navigation

### 3. Improved Navigation Bar

**Before**: Navigation hint changed on last page
**After**: Consistent navigation hint on all pages:
```
SPACE: Next page | B: Previous page | ESC: Return to menu
```

## Help System Features

### Page 1: Wiring Diagram
- ASCII art representation of SD card pinout
- Complete pin mapping from SD card to FPGA GPIO
- Verified pin assignments from PCF file
- Safety notes about voltage and grounding

### Page 2: Menu Options
- Detailed description of each menu function
- What each option does and what to expect
- Warnings for destructive operations (format)
- Status of features (implemented vs to-be-implemented)

### Page 3: Technical Reference
- Keyboard controls and navigation
- SPI speed configuration explained
- Filesystem support details (FAT12/16/32, 8.3 filenames)
- Troubleshooting tips

## Navigation Controls

- **SPACE** - Next page (loops: 1→2→3→1→...)
- **B** - Previous page (loops: 1→3→2→1→...)
- **ESC** - Return to main menu

## Code Organization Benefits

### Separation of Concerns
- Main application logic in `sd_card_manager.c`
- Help content isolated in `help.c`
- Clean interface via `help.h`

### Maintainability
- Help content updates don't require touching main application
- Easy to add more pages or modify existing content
- Reduced complexity in main file

### Reusability
- Help system could be used by other applications
- Self-contained module with no external dependencies (except incurses)

## Build Impact

- **Binary size**: 80 KB (no significant change)
- **Compilation**: help.c compiles independently
- **Linking**: help.o added to SD_FATFS_OBJS

## Testing Checklist

- [x] Compiles without errors
- [x] Links successfully
- [x] Binary size within limits (80 KB / 256 KB)
- [ ] SPACE loops from page 3 to page 1
- [ ] B loops from page 1 to page 3
- [ ] All 3 pages display correctly
- [ ] ESC returns to main menu
- [ ] Navigation bar shows on all pages

## Future Enhancements

- Add page 4 with SD card command sequences
- Include timing diagrams in ASCII art
- Add performance benchmarks/expectations
- Consider using ANSI escape codes for better formatting

## References

- Pin assignments verified from: `hdl/ice40_picorv32.pcf`
- Redraw pattern based on: `firmware/spi_test.c`
- Navigation model: Infinite circular paging

---

**Author**: Michael Wolak (mikewolak@gmail.com)
**Date**: October 2025
