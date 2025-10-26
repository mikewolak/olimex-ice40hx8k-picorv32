# Lessons Learned: Menus, Arrows, Timers, and Real-Time Display

## What I Did WRONG (and kept doing wrong)

### 0. **Menu Navigation and Arrow Keys - THE BIGGEST STRUGGLE**
- **WRONG**: Created complex `get_key_with_arrows()` helper functions
- **WRONG**: Tried to implement my own arrow key detection from scratch
- **WRONG**: Used complex timeout and buffer flushing logic
- **WRONG**: Didn't look at working menus (help.c, spi_test.c) that already work perfectly
- **WRONG**: Kept testing old binaries without verifying timestamp (wasted HOURS on this!)
- **WRONG**: Had build errors (timer_init conflict) that failed SILENTLY - binary wasn't updating!
- **THE WORST**: Built code with errors, tested hour-old binary, wondered why nothing worked

**THE RIGHT WAY** (from help.c - working example):
```c
void show_help(void) {
    int page = 0;
    int need_redraw = 1;

    flushinp();      // ALWAYS flush input buffer at start
    timeout(-1);     // Blocking mode

    while (1) {
        if (need_redraw) {
            clear();
            // ... draw menu ...
            refresh();
            need_redraw = 0;
        }

        // Simple blocking read - no complex helpers!
        int ch = getch();

        // Handle keys directly
        if (ch == 27) {  // ESC
            break;
        } else if (ch == ' ') {  // SPACE
            page = (page + 1) % (max_page + 1);
            need_redraw = 1;
        } else if (ch == 'b' || ch == 'B') {  // B
            page = (page - 1 + (max_page + 1)) % (max_page + 1);
            need_redraw = 1;
        }
    }
}
```

**For simple "press any key" menus** (from MENU_COMPARISON.md):
```c
void menu_create_test_file(void) {
    flushinp();      // ✓ CRITICAL - flush buffer first!
    timeout(-1);     // ✓ Blocking mode

    clear();
    // ... display content ...
    move(LINES - 3, 0);
    addstr("Press any key to return to menu...");
    refresh();

    getch();  // ✓ Simple blocking read - that's it!
    return;
}
```

**Arrow key handling in spi_test.c** (if you really need arrows):
```c
if (ch == 27) {  // ESC or arrow key
    timeout(10);  // Brief timeout to check for following characters
    int ch2 = getch();
    if (ch2 == '[') {
        int ch3 = getch();
        timeout(-1);
        if (ch3 == 'A') ch = KEY_UP;
        else if (ch3 == 'B') ch = KEY_DOWN;
        else if (ch3 == 'C') ch = KEY_RIGHT;
        else if (ch3 == 'D') ch = KEY_LEFT;
        else break;  // Unknown sequence
    } else {
        timeout(-1);
        break;  // Just ESC
    }
}

if (ch == 'k' || ch == KEY_UP) {
    selected = (selected - 1 + num_options) % num_options;
}
else if (ch == 'j' || ch == KEY_DOWN) {
    selected = (selected + 1) % num_options;
}
```

**CRITICAL DEBUGGING STEPS I KEPT IGNORING**:
1. **ALWAYS check binary timestamp**: `ls -lh sd_card_manager.bin && date`
2. **If screen just flickers, binary is OLD** - rebuild!
3. **Check for build errors** - silent failures happen!
4. **Look at working menus FIRST** - don't make up your own!
5. **Created MENU_COMPARISON.md** to understand why help.c works and my code didn't

### 1. **Ignoring Existing Examples**
- **WRONG**: Made up my own timer implementation without looking at working code
- **WRONG**: Created `timer_get_ticks()` approach without understanding the interrupt system
- **WRONG**: Thought I could just read a counter register without enabling the timer
- **RIGHT**: Look at `timer_clock.c` and `spi_test.c` FIRST, then copy their exact pattern

### 2. **Not Understanding the Interrupt Pattern**
- **WRONG**: Tried to calculate speed after I/O completed (showed "0 ms")
- **WRONG**: Used blocking timer reads that never worked
- **WRONG**: Didn't understand the interrupt → volatile variable → main loop check pattern
- **RIGHT**:
  ```c
  // In interrupt handler (irq_handler)
  void irq_handler(uint32_t irqs) {
      if (irqs & (1 << 0)) {  // Timer interrupt
          timer_clear_irq();  // MUST clear interrupt first!

          // Update volatile variables
          bytes_per_second = bytes_transferred_this_second;
          bytes_transferred_this_second = 0;
          timer_tick_flag = 1;  // Set flag for main loop
      }
  }

  // In main loop
  if (timer_tick_flag != last_tick_flag) {
      last_tick_flag = timer_tick_flag;
      // Update display NOW with bytes_per_second
      refresh();
  }
  ```

### 3. **Not Verifying Binary Updates**
- **WRONG**: Made changes, assumed binary was updated, tested old code
- **WRONG**: Didn't check `ls -lh binary.bin && date` after every build
- **RIGHT**: ALWAYS verify binary timestamp matches current time before testing

### 4. **Creating My Own Solutions Instead of Using Working Code**
- **WRONG**: Created custom `bench_timer_*()` functions instead of copying from examples
- **WRONG**: Made up prescaler/reload values instead of using proven ones from examples
- **RIGHT**: Copy-paste working timer setup code EXACTLY:
  ```c
  // From timer_clock.c - 60 Hz timer
  timer_init();
  timer_config(49, 16666);  // PSC=49, ARR=16666 → 60 Hz
  irq_enable();
  timer_start();

  // For 1 Hz (1 second):
  timer_config(49, 999999);  // PSC=49, ARR=999999 → 1 Hz
  ```

### 5. **Not Understanding ncurses Input Handling**
- **WRONG**: Created complex input handling without looking at working menus
- **WRONG**: Used `while (getch() == ERR)` incorrectly
- **RIGHT**: Copy exact pattern from `help.c`:
  ```c
  void menu_function(void) {
      flushinp();        // Clear input buffer FIRST
      timeout(-1);       // Blocking mode

      // ... display stuff ...

      refresh();
      int ch = getch();  // Simple blocking read
  }
  ```

## What I Finally Did RIGHT

### 1. **Timer Setup for 1 Hz Interrupts**
```c
// Configure timer for 1 Hz (1 second period)
// System clock: 50 MHz
// Prescaler: 49 (divide by 50) → 1 MHz tick rate
// Auto-reload: 999999 → 1,000,000 / 1,000,000 = 1 Hz
timer_init_bench();
timer_config_bench(49, 999999);

// Enable Timer interrupt (IRQ[0])
irq_setmask(~(1 << 0));

// Reset performance counters
bytes_transferred_this_second = 0;
bytes_per_second = 0;
timer_tick_flag = 0;

timer_start_bench();
```

### 2. **Interrupt Handler**
```c
void irq_handler(uint32_t irqs) {
    if (irqs & (1 << 0)) {  // Timer interrupt (IRQ[0])
        timer_clear_irq_bench();  // CRITICAL: Clear first!

        // Update bytes_per_second (average over last second)
        bytes_per_second = bytes_transferred_this_second;

        // Reset for next measurement period
        bytes_transferred_this_second = 0;

        // Set flag to notify main loop
        timer_tick_flag = 1;
    }
}
```

### 3. **Main Loop Real-Time Display**
```c
uint8_t last_tick_flag = 0;

for (uint32_t i = 0; i < num_blocks; i++) {
    // Do I/O
    f_write(&file, buffer, block_size, &bw);
    bytes_transferred_this_second += block_size;  // Count bytes

    // Check if timer interrupt fired - EXACTLY like timer_clock.c
    if (timer_tick_flag != last_tick_flag) {
        last_tick_flag = timer_tick_flag;

        // Update display NOW with current speed
        move(10, 0);
        format_bytes_per_sec(bytes_per_second, speed_buf, sizeof(speed_buf));
        addstr(speed_buf);
        refresh();
    }

    // Also update progress every 16 blocks for smooth animation
    if ((i & 0x0F) == 0) {
        // Update progress bar
        refresh();
    }
}
```

### 4. **Cleanup**
```c
// Stop timer and disable interrupt when done
timer_stop_bench();
irq_setmask(~0);  // Disable all interrupts
```

## Key Insights

1. **Pattern from timer_clock.c**: Main loop checks if volatile variable changed, then acts
2. **Pattern from spi_test.c**: Timer setup with prescaler/ARR for exact frequency
3. **Pattern from help.c**: Simple ncurses input with flushinp() + timeout(-1) + getch()
4. **Volatile variables**: Interrupt writes, main loop reads
5. **Always clear interrupt first**: `timer_clear_irq()` at start of handler
6. **Binary verification**: Check timestamp EVERY time before testing

## What NOT to Do

### Menus and Input:
- ❌ Don't create custom key reading functions - use simple `getch()`
- ❌ Don't forget `flushinp()` at the start of EVERY menu function
- ❌ Don't use complex timeout logic - just `timeout(-1)` for blocking
- ❌ Don't assume menus work without testing on actual hardware
- ❌ Don't test without verifying binary timestamp FIRST

### Timers and Performance:
- ❌ Don't make up your own timer/interrupt implementation
- ❌ Don't assume the binary is fresh without checking timestamp
- ❌ Don't try to "improve" working examples
- ❌ Don't use timer for blocking delays during performance measurement
- ❌ Don't calculate speed after the fact - measure in real-time with interrupts

### Build and Debug:
- ❌ Don't assume build succeeded - check for errors!
- ❌ Don't test old binaries - ALWAYS verify timestamp
- ❌ Don't waste hours on old code - check binary FIRST
- ❌ Don't ignore silent build failures (naming conflicts, etc.)

## What TO Do

- ✅ Look at working examples FIRST (timer_clock.c, spi_test.c, help.c)
- ✅ Copy patterns EXACTLY, then adapt
- ✅ Use interrupt → volatile → main loop check pattern
- ✅ Verify binary timestamp matches current time
- ✅ Use existing hardware peripheral definitions from helper files
- ✅ Keep it simple - working code > clever code
- ✅ Test after EVERY build with timestamp verification

## Summary

The right way: **Look at working examples, copy their patterns exactly, verify binary updates, test incrementally.**

The wrong way: **Make up solutions, assume binaries are fresh, ignore working code, overcomplicate things.**

This applies to:
- **Menu navigation and input**: Use help.c pattern (flushinp + timeout(-1) + simple getch)
- **Arrow key handling**: Use spi_test.c pattern (ESC sequence detection)
- **Timer interrupts**: Use timer_clock.c pattern (interrupt → volatile → main loop check)
- **Performance measurement**: Use spi_test.c pattern (interrupt-based byte counting)
- **Any other peripheral or system feature**: Find working example FIRST, copy EXACTLY

## The Pattern That Finally Worked

1. **User says**: "This doesn't work, look at help.c"
2. **I finally look**: See simple `flushinp()` + `getch()` pattern
3. **I copy exactly**: Menu works immediately
4. **User says**: "Now look at timer_clock.c for timer interrupts"
5. **I finally look**: See interrupt → volatile → main loop pattern
6. **I copy exactly**: Real-time display works immediately

**The lesson**: Stop making things up. Copy working examples. Check binary timestamps. Test incrementally.

## Files That Have Working Examples

- `help.c` - Perfect menu navigation with pages (SPACE/B keys)
- `spi_test.c` - Arrow key detection, timer setup, performance measurement
- `timer_clock.c` - Timer interrupts with 60 Hz updates, volatile variable pattern
- `MENU_COMPARISON.md` - Side-by-side comparison of working vs broken menus

**When in doubt, read these files FIRST before writing ANY code.**
