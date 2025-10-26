# SD Card Manager getch() Fix

## Problem Description

The SD card manager menu functions would flash information on screen and immediately return to the main menu without waiting for user input. The "Press any key to return to menu..." prompt would appear for a split second and then disappear.

## Root Cause

The incurses `getch()` implementation with `timeout(-1)` is **non-blocking** when no character is available in the UART buffer. It returns `ERR` (value = 1) immediately instead of blocking.

### How incurses getch() Actually Works

From `lib/incurses/incurses.c`:

```c
static int _embeddedserial_getc(int timeout_ms)
{
    // If non-blocking (timeout <= 0) and no data available, return ERR
    if (timeout_ms <= 0 && !uart_getc_available()) {
        return ERR;
    }

    // Block until data available (or return immediately if available)
    return (int)uart_getc();
}
```

With `timeout(-1)`:
- If a character IS available → calls `uart_getc()` and returns it
- If NO character is available → returns `ERR` immediately (does NOT block)

This is **counter-intuitive** compared to standard ncurses where `timeout(-1)` means "block indefinitely."

## Why spi_test.c Works

The `spi_test.c` program works correctly even though it uses the same `timeout(-1); getch()` pattern because:

1. **Main event loop implicitly handles ERR**: When `getch()` returns `ERR` (1), it doesn't match any of the character checks:
   ```c
   int ch = getch();
   if (ch == 'q' || ch == 'Q') { ... }
   else if (ch == 'h' || ch == 'H') { ... }
   else if (ch == 65 || ch == 'k') { ... }  // UP arrow
   // etc.
   ```

   Since ERR (1) doesn't match any valid key, the loop continues, redraws the screen if needed, and calls `getch()` again. This creates an **implicit polling loop**.

2. **Menu functions are inline with the main loop**: The "Test complete! (Press any key to continue)" messages in spi_test work because they're part of the main event loop, which naturally loops back.

## Why sd_card_manager Failed

The menu functions in sd_card_manager.c would call `getch()` once and then **immediately return** from the function:

```c
void menu_detect_card(void) {
    // ... display information ...

    move(LINES - 3, 0);
    addstr("Press any key to return to menu...");
    refresh();

    timeout(-1);
    getch();  // Returns ERR immediately if no key pressed
}  // ← Function returns immediately!
```

Since there's no loop in the function, when `getch()` returns `ERR`, the function exits and control returns to the main menu.

## The Fix

Add an **explicit polling loop** in menu functions that wait for user acknowledgment:

```c
void menu_detect_card(void) {
    // ... display information ...

    move(LINES - 3, 0);
    addstr("Press any key to return to menu...");
    refresh();

    timeout(-1);
    while (getch() == ERR);  // Loop until we get a real key
}
```

This creates the same polling behavior as the main event loop, but makes it explicit.

## Files Modified

### firmware/sd_fatfs/sd_card_manager.c

All menu functions that display information and wait for user input were updated:

- `menu_detect_card()` - line 196
- `menu_card_info()` - lines 224, 301
- `menu_format_card()` - lines 323, 370
- `menu_eject_card()` - line 469
- `menu_upload_overlay()` - lines 495, 547
- `menu_upload_and_execute()` - line 681
- `menu_browse_overlays()` - lines 712, 735, 749

Pattern applied:
```c
timeout(-1);
while (getch() == ERR);  // Loop until we get a real key (incurses returns ERR when no key available)
```

## TODO: Consider Fixing incurses Implementation

The current behavior where `timeout(-1)` makes `getch()` non-blocking is working as implemented, but it's **counter-intuitive** compared to standard ncurses conventions:

- **Standard ncurses**: `timeout(-1)` = block indefinitely, `timeout(0)` = non-blocking
- **Our incurses**: `timeout(-1)` = non-blocking if no data, any positive value = blocking

### Proposed Fix (NOT IMPLEMENTED)

If we want to match standard ncurses behavior, change `_embeddedserial_getc()`:

```c
static int _embeddedserial_getc(int timeout_ms)
{
    // If non-blocking (timeout == 0) and no data available, return ERR immediately
    if (timeout_ms == 0 && !uart_getc_available()) {
        return ERR;
    }

    // If blocking (timeout == -1) or timeout > 0, wait for data
    return (int)uart_getc();  // This blocks until data available
}
```

**However**: This would require updating ALL existing code that uses incurses (spi_test, etc.) to handle the new blocking behavior. For now, we're keeping incurses as-is and using the explicit polling loop pattern.

## Lessons Learned

1. **Always check for ERR when using getch()** in environments where it might not block
2. **Event loops naturally handle ERR** through their iteration, but one-shot functions need explicit loops
3. **Read the actual implementation** - don't assume library behavior matches standard conventions
4. **Testing on hardware is essential** - this issue only manifests in real-time interaction

## Author

Fix implemented: October 25, 2025
Michael Wolak (mikewolak@gmail.com)
