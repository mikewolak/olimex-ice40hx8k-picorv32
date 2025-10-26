# Documentation Index

This directory contains all project documentation files.

## Project Overview

- **../README.md** - Main project README

## Build & Development

- **BUILD_SUMMARY.md** - Build system overview
- **TESTING.md** - Testing procedures and guidelines

## Architecture & Design

- **CONTEXT_SWITCHING.md** - Context switching implementation
- **TIMER_INTEGRATION.md** - Timer system integration
- **TASK5_REVIEW.md** - Task 5 implementation review

## FreeRTOS Integration (WIP)

- **FREERTOS_STATUS.md** - FreeRTOS integration status
- **FREERTOS_WIP_NOTES.md** - Work-in-progress notes

## Lessons Learned

- **LESSONS_LEARNED.md** - Critical lessons from development
- **MENU_COMPARISON.md** - Menu implementation patterns

## SD Card / FatFS Documentation

### SD Card System (`sd_fatfs/`)

- **MEMORY_ALLOCATION.md** - Memory layout and allocation strategy
- **GETCH_FIX.md** - Input handling fixes for ncurses
- **HELP_SYSTEM.md** - Help system implementation
- **HELP_SYSTEM_REFACTOR.md** - Help system refactoring notes

### Overlay System (`sd_fatfs/`)

- **OVERLAY_SYSTEM.md** - Overlay system overview
- **OVERLAY_IMPLEMENTATION.md** - Implementation details
- **README_OVERLAY.md** - Overlay system README

### Upload Protocol (`sd_fatfs/`)

- **UPLOAD_FIX.md** - Memory allocation fixes for upload
- **UPLOAD_VERIFICATION.md** - Upload protocol verification
  - **CRITICAL**: NO printf during UART transfer!

## External Libraries

- **../firmware/lwIP/README.md** - lwIP TCP/IP stack
- **../firmware/sd_fatfs/README.md** - FatFS filesystem
- **../overlays/README.md** - Overlay binaries

## Notes

- CLAUDE.md is in the root directory and excluded from git (see .gitignore)
- This index is automatically updated when new documentation is added
