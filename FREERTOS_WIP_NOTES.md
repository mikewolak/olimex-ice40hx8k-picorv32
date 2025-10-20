# FreeRTOS Work In Progress - Stopped Point

**Date**: October 20, 2025
**Status**: Critical Bug - vTaskDelay() Not Working

---

## Problem Description

`vTaskDelay(N)` returns immediately (elapsed=0 ticks) instead of blocking the task for N ticks.

**Observed behavior**:
```
Loop #0x0000062F: Tick=0x0000015C, IRQ=0x0000015C, Calling vTaskDelay(0x00000064)...
  -> Woke: TickAfter=0x0000015C, Elapsed=0x00000000, Expected=0x00000064
```

Task should block for 100 ticks (1 second at 100 Hz), but returns on the same tick.

---

## Current Configuration

- **Tick Rate**: 100 Hz (10ms per tick) - reduced from 1000 Hz
- **Timer**: PSC=49, ARR=9999 → 50 MHz / 50 / 10000 = 100 Hz
- **FreeRTOS Heap**: 32 KB
- **Demo**: `firmware/freertos_minimal.c` - single task testing vTaskDelay

---

## Root Causes Identified

### Root Cause #1: Empty portYIELD() Macro (FIXED)

Originally `portYIELD()` was defined as an empty comment:
```c
#define portYIELD()     /* Handled by timer interrupt */
```

**Fix Applied**: Implemented portYIELD with busy-wait mechanism in `lib/freertos_port/portmacro.h:44-57`:
```c
static inline void portYIELD(void) {
    printf("DEBUG: portYIELD called, flag=1\r\n");
    xPortYieldPending = 1;
    portENABLE_INTERRUPTS();
    printf("DEBUG: Entering busy-wait loop\r\n");
    while (xPortYieldPending) {
        __asm__ volatile ("nop");
    }
    printf("DEBUG: portYIELD returned, flag cleared\r\n");
}
```

Also added `portYIELD_WITHIN_API()` definition to `lib/freertos_config/FreeRTOSConfig.h:87`:
```c
#define portYIELD_WITHIN_API() portYIELD()
```

**Status**: Implemented but not being called (no debug output appears).

---

## Root Cause #2: portYIELD() Not Being Called (CURRENT ISSUE)

### Investigation Results

Traced the call path through FreeRTOS kernel (`downloads/freertos/tasks.c`):

1. **vTaskDelay() flow** (lines 2464-2493):
   ```c
   void vTaskDelay( const TickType_t xTicksToDelay )
   {
       BaseType_t xAlreadyYielded = pdFALSE;

       if( xTicksToDelay > ( TickType_t ) 0U )
       {
           vTaskSuspendAll();  // Suspend scheduler
           {
               prvAddCurrentTaskToDelayedList( xTicksToDelay, pdFALSE );
           }
           xAlreadyYielded = xTaskResumeAll();  // Line 2482 - Resume scheduler
       }

       if( xAlreadyYielded == pdFALSE )  // Line 2491 - THIS IS NOT BEING TAKEN
       {
           taskYIELD_WITHIN_API();  // Would call our portYIELD()
       }
   }
   ```

2. **xTaskResumeAll() flow** (lines 4011-4157):
   ```c
   BaseType_t xTaskResumeAll( void )
   {
       BaseType_t xAlreadyYielded = pdFALSE;

       // ... process pending ready tasks ...
       // ... process pended ticks ...

       if( xYieldPendings[ xCoreID ] != pdFALSE )  // Line 4126
       {
           #if ( configUSE_PREEMPTION != 0 )
           {
               xAlreadyYielded = pdTRUE;  // Line 4130 - Set to TRUE
           }
           #endif

           #if ( configNUMBER_OF_CORES == 1 )
           {
               taskYIELD_TASK_CORE_IF_USING_PREEMPTION( pxCurrentTCB );  // Line 4136
           }
           #endif
       }

       return xAlreadyYielded;  // Returns TRUE, preventing portYIELD call
   }
   ```

3. **taskYIELD_TASK_CORE_IF_USING_PREEMPTION** (lines 81-85):
   ```c
   #define taskYIELD_TASK_CORE_IF_USING_PREEMPTION( pxTCB ) \
       do {                                                  \
           ( void ) ( pxTCB );                               \
           portYIELD_WITHIN_API();                           \
       } while( 0 )
   ```

### The Problem

The `if( xYieldPendings[ xCoreID ] != pdFALSE )` condition at line 4126 appears to be **FALSE**, so:
- The `taskYIELD_TASK_CORE_IF_USING_PREEMPTION()` macro is NOT called
- `xAlreadyYielded` remains FALSE
- But somehow it's still returning TRUE (or the yield at line 4136 is failing silently)
- This causes the `if( xAlreadyYielded == pdFALSE )` check at vTaskDelay:2491 to be skipped
- `portYIELD()` is never called
- Task returns immediately without blocking

---

## What's Missing

### Theory 1: xYieldPendings[] Not Being Set

The `xYieldPendings[]` array should be set to TRUE by:
- `xTaskIncrementTick()` when a delayed task becomes ready
- `prvAddTaskToReadyList()` when a higher priority task becomes ready

**Need to investigate**:
- Is `prvAddCurrentTaskToDelayedList()` actually adding the task to the delayed list?
- Is `xTaskIncrementTick()` checking the delayed list correctly?
- Why isn't a yield being requested when the task blocks?

### Theory 2: Context Switch Not Happening

Even if `portYIELD_WITHIN_API()` is called at line 4136, the context switch might be failing:
- Our `portYIELD()` uses a busy-wait loop waiting for timer interrupt
- But we're inside `taskENTER_CRITICAL()` / `taskEXIT_CRITICAL()` (line 4027/4151)
- Critical sections disable interrupts via `portDISABLE_INTERRUPTS()`
- So timer interrupt can't fire during the busy-wait!

**This might be the actual root cause!**

---

## Next Steps to Debug

### Option 1: Add Debug Output to FreeRTOS Kernel (HACKY)

Modify `downloads/freertos/tasks.c` to add printf statements:
- Before/after `prvAddCurrentTaskToDelayedList()` call
- At line 4126 to show `xYieldPendings[xCoreID]` value
- At line 4130 to confirm `xAlreadyYielded` is being set
- At line 4136 before calling `taskYIELD_TASK_CORE_IF_USING_PREEMPTION()`

**Problem**: Modifying kernel source is ugly and error-prone.

### Option 2: Rethink portYIELD() Implementation (RECOMMENDED)

Our current `portYIELD()` uses a busy-wait loop:
```c
static inline void portYIELD(void) {
    xPortYieldPending = 1;
    portENABLE_INTERRUPTS();
    while (xPortYieldPending) {
        __asm__ volatile ("nop");
    }
}
```

**Problem**: This is called from within `taskENTER_CRITICAL()` / `taskEXIT_CRITICAL()` blocks where interrupts are disabled!

**Better approach**: `portYIELD()` should just trigger a software interrupt or set a flag, and let the next timer interrupt perform the actual context switch:

```c
static inline void portYIELD(void) {
    // Just set flag - don't wait
    xPortYieldPending = 1;
    // Context switch will happen on next timer interrupt
}
```

But this won't work either because the task will continue executing after `vTaskDelay()` returns!

### Option 3: Use Timer Interrupt to Force Immediate Context Switch

Modify `portYIELD()` to manually trigger a timer interrupt:
```c
static inline void portYIELD(void) {
    // Force timer interrupt to fire immediately
    TIMER_SR = TIMER_SR_UIF;  // Set interrupt flag
    portENABLE_INTERRUPTS();
    // Wait a bit for interrupt to fire
    for (volatile int i = 0; i < 100; i++) __asm__ volatile ("nop");
}
```

**Problem**: Still won't work inside critical section.

### Option 4: Research Standard RISC-V FreeRTOS Ports

Look at how official RISC-V ports handle `portYIELD()`:
- Do they use software interrupts?
- Do they use `ecall` instruction?
- How do they handle yielding from within critical sections?

**Problem**: PicoRV32 doesn't support standard RISC-V interrupts/exceptions.

---

## Key Files Modified

### lib/freertos_port/portmacro.h
- Lines 40-57: Implemented `portYIELD()` with busy-wait and debug output
- Line 42: Added `extern volatile uint32_t xPortYieldPending;`
- Line 43: Added `extern int printf(...);` for debug

### lib/freertos_port/freertos_irq.c
- Line 60: Changed `TIMER_AUTO_RELOAD` from 999 to 9999 (100 Hz tick)
- Line 101: Added `volatile uint32_t xPortYieldPending = 0;`
- Lines 142-145: Check and clear `xPortYieldPending` in IRQ handler

### lib/freertos_config/FreeRTOSConfig.h
- Line 87: Added `#define portYIELD_WITHIN_API() portYIELD()`

### .config and configs/defconfig
- Line 67: Changed `CONFIG_FREERTOS_TICK_RATE_HZ=100` (was 1000)

### firmware/freertos_minimal.c
- Lines 38-74: Added comprehensive diagnostic output showing tick counts before/after delay

---

## Hardware Testing Results

User reported (from serial output):
```
Loop #0x0000062F: Tick=0x0000015C, IRQ=0x0000015C, Calling vTaskDelay(0x00000064)...
  -> Woke: TickAfter=0x0000015C, Elapsed=0x00000000, Expected=0x00000064
```

**Observations**:
- Tick counter incrementing correctly (Tick ≈ IRQ count)
- Timer interrupts firing at correct rate
- `vTaskDelay(100)` returns immediately (Elapsed=0)
- **No "DEBUG:" messages from portYIELD()** - confirms it's not being called

---

## Recommended Next Steps

1. **Verify critical section behavior**: Add debug output to confirm interrupts are disabled during `taskENTER_CRITICAL()` / `taskEXIT_CRITICAL()`

2. **Rethink yield mechanism**: PicoRV32 port might need a completely different approach to yielding:
   - Maybe don't use busy-wait at all
   - Maybe rely entirely on timer interrupt for context switching
   - Maybe implement cooperative scheduling only (no preemption from vTaskDelay)

3. **Study working bare-metal demo**: The `firmware/coop_tasks.c` demo implements simple cooperative multitasking without FreeRTOS. Study how it handles task switching and see if we can apply similar principles.

4. **Consider if single-task scenario is edge case**: With only one user task + idle task:
   - There's nothing to switch TO when blocking
   - Maybe FreeRTOS is "optimizing" by not yielding?
   - Test with 2+ user tasks to see if behavior changes

5. **Check FreeRTOS configuration**: Verify all config options are correct:
   - `configUSE_PREEMPTION` = 1 (enabled)
   - `configUSE_TIME_SLICING` = 1 (enabled)
   - `configNUMBER_OF_CORES` = 1
   - Are there any other config options affecting yield behavior?

---

## Contact

Michael Wolak
mikewolak@gmail.com
mike@epromfoundry.com

## Repository

https://github.com/mikewolak/olimex-ice40hx8k-picorv32.git
Branch: master
Last commit before WIP: 8fb01e3 "Update minicom README with production-ready status and author info"
