# Claude Code Session Notes - FreeRTOS Integration

## Current Status: Work In Progress (DO NOT COMMIT YET)

**Last Commit**: `8fb01e3 Update minicom README with production-ready status and author info`

**NO COMMITS have been made for FreeRTOS work** - everything is staged as WIP.

---

## What We're Building

**Goal**: Integrate FreeRTOS RTOS into PicoRV32 platform for multitasking support.

**Challenge**: PicoRV32 uses **custom interrupt instructions** incompatible with standard RISC-V, so we cannot use FreeRTOS's standard RISC-V port. We must create a custom port.

**Architecture Decision**: Use existing proven PicoRV32 interrupt infrastructure from working examples (timer_clock.c, hexedit_fast.c) and adapt FreeRTOS to it.

---

## PicoRV32 Custom Interrupt System (CRITICAL)

### Custom Instructions (Must Use These)
```assembly
# Set IRQ mask (enable/disable interrupts)
.insn r 0x0B, 6, 3, rd, rs1, x0    # maskirq rd, rs1
# Example: maskirq x0, x0 = enable all (mask=0)
#          maskirq x0, ~0 = disable all (mask=0xFFFFFFFF)

# Read IRQ status
.insn r 0x0B, 4, 0, rd, x1, x0     # getq rd, q1

# Return from interrupt (MUST use instead of mret)
.insn r 0x0B, 0, 2, x0, x0, x0     # retirq
```

### C Inline Functions (Already Working)
```c
// From firmware/hexedit_fast.c and timer_clock.c
static inline void irq_enable(void) {
    uint32_t dummy;
    __asm__ volatile (".insn r 0x0B, 6, 3, %0, %1, x0" : "=r"(dummy) : "r"(0));
}

static inline void irq_disable(void) {
    uint32_t dummy;
    __asm__ volatile (".insn r 0x0B, 6, 3, %0, %1, x0" : "=r"(dummy) : "r"(~0));
}
```

### IRQ Vector (firmware/start.S)
- **Location**: 0x10 (PROGADDR_IRQ - defined in linker.ld)
- **Entry point**: `irq_vec` label at line 17
- **What it does**:
  1. Saves all caller-saved registers (ra, a0-a7, t0-t6) to stack
  2. Reads IRQ mask with `getq a0, q1`
  3. Calls `irq_handler()` (weak symbol, can be overridden)
  4. Restores all registers
  5. Returns with `retirq` instruction

### Timer Peripheral (0x80000020)
```c
#define TIMER_BASE     0x80000020
#define TIMER_CTRL     (*(volatile uint32_t*)(TIMER_BASE + 0x00))
#define TIMER_COMPARE  (*(volatile uint32_t*)(TIMER_BASE + 0x04))
#define TIMER_COUNTER  (*(volatile uint32_t*)(TIMER_BASE + 0x08))

// Timer control bits
#define TIMER_ENABLE    (1 << 0)
#define TIMER_IRQ_ENABLE (1 << 1)

// For 1ms tick at 50MHz: COMPARE = 50000
```

---

## What's Been Completed

### 1. Kconfig Integration ✅
**File**: `Kconfig` (lines 156-200)

Added FreeRTOS menu after Newlib section with:
- `CONFIG_FREERTOS` - Enable/disable (default: **n** - disabled until ready)
- `CONFIG_FREERTOS_CPU_CLOCK_HZ` - 50000000 (50 MHz)
- `CONFIG_FREERTOS_TICK_RATE_HZ` - 1000 (1 KHz = 1ms tick)
- `CONFIG_FREERTOS_MAX_PRIORITIES` - 5
- `CONFIG_FREERTOS_MINIMAL_STACK_SIZE` - 128 words
- `CONFIG_FREERTOS_TOTAL_HEAP_SIZE` - 16384 bytes (16 KB)

**Why these values**:
- 50 MHz: System clock from FPGA
- 1000 Hz tick: Standard 1ms resolution, good balance
- 5 priorities: Enough for most applications, not excessive
- 128 words stack: Conservative minimum (512 bytes)
- 16 KB heap: Conservative for 512KB total SRAM

**Automatically selects**: `BUILD_NEWLIB=y` (FreeRTOS requires C library)

### 2. Configuration Header ✅
**File**: `lib/freertos_config/FreeRTOSConfig.h`

**Uses CONFIG_* variables** (NOT hard-coded):
```c
#define configCPU_CLOCK_HZ       CONFIG_FREERTOS_CPU_CLOCK_HZ
#define configTICK_RATE_HZ       CONFIG_FREERTOS_TICK_RATE_HZ
#define configMAX_PRIORITIES     CONFIG_FREERTOS_MAX_PRIORITIES
// etc...
```

**Critical**: Build system MUST pass these as `-D` flags:
```makefile
CFLAGS += -DCONFIG_FREERTOS_CPU_CLOCK_HZ=50000000
CFLAGS += -DCONFIG_FREERTOS_TICK_RATE_HZ=1000
# etc...
```

Minimal configuration:
- Preemptive scheduling enabled
- Mutexes and counting semaphores enabled
- Heap_4 allocator (standard coalescent)
- No idle/tick hooks (keep simple initially)
- Malloc failed hook enabled (safety)
- Newlib reentrant support enabled

### 3. Port Layer (Partial) ✅
**Directory**: `lib/freertos_port/`

**Files Created**:

#### `portmacro.h`
- RV32I type definitions
- PicoRV32 IRQ control inline functions
- Port-specific macros
- Critical: `picorv32_maskirq()` and `picorv32_getirq()` functions

#### `port.c`
- `pxPortInitialiseStack()` - Sets up initial task stack
- `xPortStartScheduler()` - **INCOMPLETE** (just enables interrupts)
- `vPortEnterCritical()` / `vPortExitCritical()` - Uses `maskirq()`
- `vApplicationMallocFailedHook()` - Hangs on failure

**What's MISSING**:
- Actual scheduler start (load first task, jump to it)
- Context switch mechanism
- Integration with `irq_vec` in start.S

### 4. Test Firmware ✅
**File**: `firmware/freertos_minimal.c`

Minimal proof-of-concept that just:
- Includes FreeRTOS headers (validates config)
- Prints message to UART
- Infinite loop (no scheduler yet)

**Purpose**: Verify build system compiles FreeRTOS headers correctly.

### 5. Status Documentation ✅
**File**: `FREERTOS_STATUS.md`

Complete description of what's done and what's needed.

---

## What Needs To Be Done (Priority Order)

### IMMEDIATE: Task 1 - Build System Integration
**Why First**: Can't test anything until it builds

**Files to modify**:
1. Top-level `Makefile` - Add FreeRTOS download targets
2. `firmware/Makefile` - Add FreeRTOS build support

**Top-level Makefile additions**:
```makefile
# Add after newlib section

FREERTOS_DIR = downloads/freertos

freertos-download:
	@if [ ! -d "$(FREERTOS_DIR)" ]; then \
		git clone --depth 1 --branch main \
		https://github.com/FreeRTOS/FreeRTOS-Kernel.git $(FREERTOS_DIR); \
	fi

freertos-clean:
	@rm -rf $(FREERTOS_DIR)
```

**firmware/Makefile additions**:
```makefile
# FreeRTOS paths
FREERTOS_DIR = ../downloads/freertos
FREERTOS_PORT = ../lib/freertos_port
FREERTOS_CONFIG = ../lib/freertos_config

# When USE_FREERTOS=1 is set:
ifeq ($(USE_FREERTOS),1)
    # Force newlib
    USE_NEWLIB = 1

    # Add FreeRTOS includes
    CFLAGS += -I$(FREERTOS_DIR)/include
    CFLAGS += -I$(FREERTOS_PORT)
    CFLAGS += -I$(FREERTOS_CONFIG)

    # Pass CONFIG_* values as -D flags (read from .config)
    CFLAGS += -DCONFIG_FREERTOS_CPU_CLOCK_HZ=50000000
    CFLAGS += -DCONFIG_FREERTOS_TICK_RATE_HZ=1000
    CFLAGS += -DCONFIG_FREERTOS_MAX_PRIORITIES=5
    CFLAGS += -DCONFIG_FREERTOS_MINIMAL_STACK_SIZE=128
    CFLAGS += -DCONFIG_FREERTOS_TOTAL_HEAP_SIZE=16384

    # FreeRTOS kernel sources
    FREERTOS_SRCS = \
        $(FREERTOS_DIR)/tasks.c \
        $(FREERTOS_DIR)/queue.c \
        $(FREERTOS_DIR)/list.c \
        $(FREERTOS_DIR)/timers.c \
        $(FREERTOS_DIR)/portable/MemMang/heap_4.c \
        $(FREERTOS_PORT)/port.c

    # Compile to objects
    FREERTOS_OBJS = $(FREERTOS_SRCS:.c=.o)

    # Add to link
    LIBS := $(FREERTOS_OBJS) $(LIBS)
endif

# Target for freertos_minimal
freertos_minimal:
	$(MAKE) TARGET=freertos_minimal USE_FREERTOS=1 USE_NEWLIB=1 single-target
```

### Task 2 - Test Minimal Build
**After build system is ready**:

```bash
# Download FreeRTOS
make freertos-download

# Try building minimal test
cd firmware
make freertos_minimal

# Expected: Should compile and link (even if non-functional)
# This validates: Kconfig → headers → build system flow
```

**If this works**: Foundation is solid, proceed to Task 3.

**If this fails**: Fix build system before proceeding.

### Task 3 - Timer Tick Integration
**Why**: FreeRTOS needs periodic tick for scheduling

**Modify**: `firmware/start.S` or create new IRQ handler

**Approach**: Extend existing `irq_handler` weak symbol

Create `lib/freertos_port/freertos_irq.c`:
```c
#include <FreeRTOS.h>
#include <task.h>

#define TIMER_BASE     0x80000020
#define TIMER_CTRL     (*(volatile uint32_t*)(TIMER_BASE + 0x00))
#define TIMER_SR       (*(volatile uint32_t*)(TIMER_BASE + 0x04))
#define TIMER_COMPARE  (*(volatile uint32_t*)(TIMER_BASE + 0x0C))
#define TIMER_SR_UIF   (1 << 0)

#define TIMER_TICKS_PER_MS  (50000000 / 1000)  // 50 MHz / 1000 = 50000

void freertos_timer_init(void) {
    TIMER_CTRL = 0;                    // Disable
    TIMER_COMPARE = TIMER_TICKS_PER_MS; // 1ms tick
    TIMER_SR = TIMER_SR_UIF;           // Clear flag
    TIMER_CTRL = 0x03;                  // Enable + IRQ enable
}

// Override weak irq_handler from start.S
void irq_handler(void) {
    uint32_t timer_status = TIMER_SR;

    if (timer_status & TIMER_SR_UIF) {
        // Clear timer interrupt
        TIMER_SR = TIMER_SR_UIF;

        // Increment FreeRTOS tick
        if (xTaskIncrementTick() != pdFALSE) {
            // Context switch needed
            vTaskSwitchContext();
        }
    }
}
```

### Task 4 - Context Switching (HARDEST PART)
**Why**: FreeRTOS needs to save/restore task contexts

**Challenge**: Must integrate with PicoRV32's `irq_vec` and `retirq`

**Two approaches**:

#### Option A: Cooperative (Simpler, Start Here)
- Only switch contexts in timer interrupt
- Modify `irq_vec` to save context to current task's stack
- After `irq_handler()` returns, restore from (potentially different) task's stack

#### Option B: Full Preemptive (Later)
- Add software interrupt for `portYIELD()`
- More complex but fully preemptive

**Start with Option A**, get it working, then enhance.

**Needed**: Modify `start.S` `irq_vec`:
```assembly
irq_vec:
    # Save context to pxCurrentTCB->pxTopOfStack
    # (32 registers)

    # Call irq_handler (which may call vTaskSwitchContext)
    call irq_handler

    # Restore context from pxCurrentTCB->pxTopOfStack
    # (may be different task now)

    retirq
```

### Task 5 - Implement xPortStartScheduler
**In**: `lib/freertos_port/port.c`

Must:
1. Initialize timer for tick
2. Enable interrupts
3. Load first task's context from `pxCurrentTCB->pxTopOfStack`
4. Jump to it (simulate return from interrupt)

### Task 6 - Create Real Demo
**After everything above works**:

Create `firmware/freertos_demo.c`:
- 2 tasks blinking different LEDs
- Both print to UART
- Demonstrates multitasking

### Task 7 - Test, Debug, Iterate
**DO NOT COMMIT until this works**

Test checklist:
- [ ] Compiles without errors
- [ ] Links without undefined symbols
- [ ] Loads on FPGA
- [ ] Timer interrupt fires
- [ ] Tasks actually switch
- [ ] UART shows messages from both tasks
- [ ] No crashes or hangs

**Only after all tests pass**: Commit with detailed message.

---

## Reference Files (Working Examples)

Study these for PicoRV32 interrupt handling:

1. **firmware/start.S** (lines 1-101)
   - `irq_vec` at line 17: IRQ entry point
   - Register save/restore pattern
   - `retirq` usage at line 63

2. **firmware/timer_clock.c** (lines 1-240)
   - Timer initialization (lines 64-76)
   - Timer interrupt handling
   - `irq_enable()` inline function (lines 40-43)

3. **firmware/hexedit_fast.c**
   - `irq_enable()` / `irq_disable()` implementations
   - Real-world interrupt usage

4. **firmware/linker.ld**
   - Memory layout
   - IRQ vector placement
   - Stack location

---

## Directory Structure

```
olimex-ice40hx8k-picorv32/
├── Kconfig                          # Modified: FreeRTOS menu added
├── .config                          # Not modified yet (FREERTOS=n)
├── FREERTOS_STATUS.md               # Status documentation
├── CLAUDE.md                        # This file
├── downloads/
│   └── freertos/                    # Will be downloaded
│       ├── tasks.c
│       ├── queue.c
│       ├── list.c
│       ├── timers.c
│       ├── portable/MemMang/heap_4.c
│       └── include/
│           ├── FreeRTOS.h
│           ├── task.h
│           └── ...
├── lib/
│   ├── freertos_config/
│   │   └── FreeRTOSConfig.h         # Created: CONFIG_* references
│   └── freertos_port/
│       ├── portmacro.h              # Created: PicoRV32 types/macros
│       ├── port.c                   # Created: Incomplete port
│       └── freertos_irq.c           # TODO: Timer tick handler
└── firmware/
    ├── start.S                      # Study: IRQ vector
    ├── timer_clock.c                # Study: Timer example
    ├── hexedit_fast.c               # Study: IRQ control
    ├── freertos_minimal.c           # Created: Build test
    ├── freertos_demo.c              # TODO: Real demo
    └── Makefile                     # TODO: Add FreeRTOS build
```

---

## Build Commands (Once Ready)

```bash
# 1. Download FreeRTOS kernel
make freertos-download

# 2. Build minimal test (validates build system)
cd firmware
make freertos_minimal

# 3. Later: Build real demo
make freertos_demo

# 4. Upload to FPGA
../tools/uploader/fw_upload_fast freertos_demo.bin
```

---

## Common Pitfalls to Avoid

1. **Don't hard-code CONFIG values** - Use Kconfig + -D flags
2. **Don't use standard RISC-V `mret`** - Must use PicoRV32 `retirq`
3. **Don't assume CLINT/MTIME exists** - PicoRV32 has custom timer
4. **Don't modify start.S casually** - It's critical for all firmware
5. **Don't commit until build tested** - We learned this the hard way

---

## Why This Approach

1. **Use Kconfig properly** - Values in .config, referenced in code, passed as -D flags
2. **Leverage existing code** - PicoRV32 interrupt system already works
3. **Incremental development** - Build system → minimal test → timer → context switch → demo
4. **Test before commit** - NO COMMITS until proven working

---

## Next Session Start Here

1. Read this file completely
2. Review FREERTOS_STATUS.md
3. Start with **Task 1: Build System Integration**
4. Then **Task 2: Test Minimal Build**
5. Don't proceed to Task 3+ until Tasks 1-2 work

**Remember**: NO COMMITS until full working build and test!

---

## Git Repository Information

**Repository**: https://github.com/mikewolak/olimex-ice40hx8k-picorv32.git
**Branch**: master
**Remote**: origin (git@github.com:mikewolak/olimex-ice40hx8k-picorv32.git)

### Current Git Status
- **Current tag**: 0.11 (Minicom-FPGA integration)
- **Last commit**: 8fb01e3 "Update minicom README with production-ready status and author info"
- **Uncommitted files**: All FreeRTOS work (intentionally WIP)

### 🚨 CRITICAL GIT COMMIT RULES 🚨

**DO NOT ADD CLAUDE AS A CONTRIBUTOR TO GIT COMMITS!!!**

**Never include**:
- "Co-Authored-By: Claude <noreply@anthropic.com>"
- "🤖 Generated with [Claude Code]..."
- Any similar attribution text

**Commit messages should contain ONLY**:
- Summary line
- Blank line
- Detailed description of changes

**NO exceptions. NO attribution. EVER.**

This is from user's `.claude/CLAUDE.md` global configuration.

### Commit Format

```
Summary line describing the change

Detailed description of what was changed and why.
Can be multiple paragraphs explaining the implementation,
design decisions, and any important notes.

Technical details, file changes, testing performed, etc.
```

**Example Good Commit**:
```
Add FreeRTOS integration for PicoRV32

Implemented custom FreeRTOS port for PicoRV32 RISC-V core using
the existing interrupt infrastructure based on custom instructions.

Components:
- Kconfig integration with sensible defaults
- FreeRTOSConfig.h using CONFIG_* variables
- Custom port layer for PicoRV32 IRQ system
- Timer integration for scheduler tick
- Demo application with 2 tasks

Tested on hardware with successful multitasking.
```

**Example BAD Commit** (DON'T DO THIS):
```
Add FreeRTOS integration for PicoRV32

...description...

🤖 Generated with [Claude Code](https://claude.com/claude-code)

Co-Authored-By: Claude <noreply@anthropic.com>
```

---

## Contact & Context

- **Author**: Michael Wolak (mikewolak@gmail.com, mike@epromfoundry.com)
- **Project**: Olimex iCE40HX8K-EVB with PicoRV32 RISC-V
- **Platform**: 512KB SRAM, 50 MHz, RV32IM ISA
- **Purpose**: Educational RISC-V FPGA platform
- **Date**: October 2025
