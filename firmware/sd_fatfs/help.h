//==============================================================================
// Olimex iCE40HX8K-EVB RISC-V Platform
// SD Card Manager - Help System Header
//
// Interactive multi-page help system for SD card wiring and usage
//
// Copyright (c) October 2025 Michael Wolak
// Email: mikewolak@gmail.com, mike@epromfoundry.com
//==============================================================================

#ifndef HELP_H
#define HELP_H

//==============================================================================
// Function Prototypes
//==============================================================================

// Show interactive help system
// - 3 pages: Wiring diagram, Menu options, Technical reference
// - SPACE: Next page (loops infinitely)
// - B: Previous page (loops infinitely)
// - ESC: Return to main menu
void show_help(void);

#endif // HELP_H
