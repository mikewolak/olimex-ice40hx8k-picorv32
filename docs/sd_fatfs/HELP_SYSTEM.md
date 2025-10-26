# SD Card Manager - Interactive Help System

## Overview

Added comprehensive 3-page interactive help system accessible with 'H' key from main menu.

## Features

### Page 1: Wiring Diagram
- ASCII art representation of SD card pinout
- Complete pin mapping from SD card to FPGA GPIO
- Verified pin assignments from PCF file:
  - **SPI_SCK** (F5) → SD Card CLK
  - **SPI_MOSI** (B1) → SD Card DI  
  - **SPI_MISO** (C1) → SD Card DO
  - **SPI_CS** (C2) → SD Card CS
- Important safety notes about voltage and grounding

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

## Navigation

- **SPACE** - Next page
- **B** - Back to previous page  
- **ESC** - Return to main menu

Page indicator shows current position (e.g., "Page 1/3")

## Implementation

### Code Structure
```c
void show_help(void) {
    int page = 0;
    int max_page = 2;  // 3 pages total
    
    while (1) {
        clear();
        
        // Render current page
        if (page == 0) {
            // Wiring diagram with ASCII art
        } else if (page == 1) {
            // Menu options
        } else if (page == 2) {
            // Technical info
        }
        
        // Navigation
        int ch = getch();
        if (ch == 27) break;           // ESC
        if (ch == ' ') page++;         // SPACE
        if (ch == 'b') page--;         // B
    }
}
```

### Integration Points

1. **Main menu**: Added 'H' key handler
2. **Help hint**: Shows "Press 'H' for Help with wiring diagram" above status bar
3. **Refresh**: Forces full redraw when returning from help

## Pin Verification

Verified against `/home/mwolak/olimex-ice40hx8k-picorv32/hdl/ice40_picorv32.pcf`:

```pcf
set_io SPI_SCK  F5    # SPI Clock Output
set_io SPI_MOSI B1    # SPI Master Out Slave In
set_io SPI_MISO C1    # SPI Master In Slave Out
set_io SPI_CS   C2    # SPI Chip Select (Active Low)
```

## Code Size Impact

- **Before help system**: 75 KB
- **After help system**: 80 KB
- **Increase**: ~5 KB (well within budget)

## Benefits

1. **Self-documenting**: No need for external wiring guide
2. **Always available**: Built into the firmware
3. **Contextual**: Explains features in the app itself
4. **Educational**: Great for new users learning the platform

## Future Enhancements

- Add page 4 with SD card command sequences
- Include timing diagrams in ASCII art
- Add performance benchmarks/expectations
- Link to external resources via QR codes (if display supports)

## Example Session

```
User presses 'H' → Page 1 (Wiring)
User presses SPACE → Page 2 (Menu Options)  
User presses SPACE → Page 3 (Technical)
User presses ESC → Back to main menu
```

Clean, intuitive navigation following the spi_test.c help pattern.
