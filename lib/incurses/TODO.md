# incurses Library TODO

## Current Behavior (Working as Implemented)

The incurses `getch()` function with `timeout(-1)` is **non-blocking**:
- Returns `ERR` immediately if no character is available
- Only blocks if `uart_getc_available()` returns true

This is counter-intuitive compared to standard ncurses where `timeout(-1)` means "block indefinitely."

## Proposed Enhancement (Optional)

Consider making `timeout(-1)` behave like standard ncurses:
- `timeout(-1)` → Block indefinitely (wait for a key)
- `timeout(0)` → Non-blocking (return ERR if no key)
- `timeout(N)` where N > 0 → Wait up to N milliseconds

### Implementation Suggestion

Modify `_embeddedserial_getc()` in `incurses.c`:

```c
static int _embeddedserial_getc(int timeout_ms)
{
    /* Direct UART access for unbuffered input */

    // If non-blocking (timeout == 0) and no data available, return ERR immediately
    if (timeout_ms == 0 && !uart_getc_available()) {
        return ERR;
    }

    // If blocking (timeout == -1) or timeout > 0, wait for data
    // (timeout > 0 could implement actual timeout in the future)
    return (int)uart_getc();  // This blocks until data available
}
```

## Impact Assessment

**Before making this change**, review ALL programs using incurses:
- `firmware/spi_test.c`
- `firmware/sd_fatfs/sd_card_manager.c`
- `firmware/hexedit_fast.c`
- `firmware/timer_clock.c`
- Any other programs using `#include "../lib/incurses/curses.h"`

Programs may rely on the current non-blocking behavior with implicit polling loops.

## Current Workaround (Implemented)

Programs that need blocking behavior use explicit polling:

```c
timeout(-1);
while (getch() == ERR);  // Loop until we get a real key
```

This pattern works correctly with the current implementation.

## Decision

**No change required** - the current implementation is working as designed. Programs are adapted to handle the non-blocking behavior appropriately.

If future requirements demand standard ncurses blocking behavior, revisit this TODO.

## Notes

- Documented in `firmware/sd_fatfs/GETCH_FIX.md`
- October 25, 2025 - SD card manager fixed to work with current behavior
