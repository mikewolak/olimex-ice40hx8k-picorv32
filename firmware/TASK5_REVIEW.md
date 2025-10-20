# Task 5 Implementation Review: xPortStartScheduler()

## Objective
Implement the scheduler startup mechanism to jump from main() to the first FreeRTOS task without returning.

## Problem Analysis

### The Challenge
When `vTaskStartScheduler()` is called from main():
1. FreeRTOS has created tasks and initialized their stacks
2. `pxCurrentTCB` points to the first task to run
3. Timer is configured but tasks aren't running yet
4. We need to "jump" into the first task's context

### Key Insight
The task stack was initialized by `pxPortInitialiseStack()` with:
- Stack offset 0: `ra` = task entry point (e.g., `vTask1_FastBlink`)
- Stack offset 4: `a0` = task parameter
- Stack offsets 8-60: other registers (zeroed)

This is **exactly** the format that `irq_vec` expects when restoring context!

### The Solution
Simulate an interrupt return to "fake" being inside the task:
1. Load the task's stack pointer from `pxCurrentTCB->pxTopOfStack`
2. Restore all 16 caller-saved registers from that stack
3. Execute `retirq` which will:
   - Re-enable interrupts
   - Jump to the address in `ra` (the task entry point)

## Implementation

### File 1: firmware/startFRT.S (lines 113-172)

Added `vPortStartFirstTask()` assembly function:

```assembly
.global vPortStartFirstTask
vPortStartFirstTask:
    /* Load pxCurrentTCB (pointer to first task's TCB) */
    la t0, pxCurrentTCB     // t0 = &pxCurrentTCB
    lw t0, 0(t0)            // t0 = pxCurrentTCB (pointer to TCB)

    /* Load task's stack pointer from TCB->pxTopOfStack (first field) */
    lw sp, 0(t0)            // sp = pxCurrentTCB->pxTopOfStack

    /* Restore ALL caller-saved registers from task's stack */
    lw ra,  0(sp)           // ra = task entry point!
    lw a0,  4(sp)           // a0 = task parameter
    lw a1,  8(sp)
    lw a2, 12(sp)
    lw a3, 16(sp)
    lw a4, 20(sp)
    lw a5, 24(sp)
    lw a6, 28(sp)
    lw a7, 32(sp)
    lw t0, 36(sp)
    lw t1, 40(sp)
    lw t2, 44(sp)
    lw t3, 48(sp)
    lw t4, 52(sp)
    lw t5, 56(sp)
    lw t6, 60(sp)
    addi sp, sp, 64         // Pop stack frame

    /* "Return" from interrupt to first task */
    .insn r 0x0B, 0, 2, x0, x0, x0  // retirq
```

**Disassembly verification (freertos_demo.lst):**
- Address: `0x000000fc`
- Size: 84 bytes
- retirq instruction: `0x0400000b` at address `0x150`
- References pxCurrentTCB at `0x79a0` (BSS)

### File 2: lib/freertos_port/port.c (lines 57-86)

Modified `xPortStartScheduler()`:

```c
/* Implemented in startFRT.S */
extern void vPortStartFirstTask(void) __attribute__((noreturn));

BaseType_t xPortStartScheduler(void)
{
    /* Initialize timer for tick generation (1 KHz = 1 ms tick) */
    vPortSetupTimerInterrupt();

    /* CRITICAL: Enable interrupts BEFORE jumping to first task */
    picorv32_maskirq(0);

    /* Jump to first task - NEVER RETURNS! */
    vPortStartFirstTask();

    /* Should NEVER reach here */
    return pdFALSE;
}
```

**Disassembly verification (freertos_demo.lst):**
- Address: `0x00002cc8`
- Calls `vPortSetupTimerInterrupt()` at `0x2d30`
- Executes `picorv32_maskirq(0)` via inline assembly: `0x0607e78b`
- Calls `vPortStartFirstTask()` at `0x0fc`: `c20fd0ef jal ra,fc`
- **NOTE**: The `__attribute__((noreturn))` tells compiler vPortStartFirstTask never returns

## Execution Flow

### Before vPortStartFirstTask():
```
Stack:              main's stack (at __stack_top)
SP:                 0x80000 (top of SRAM)
PC:                 0x2cdc (inside xPortStartScheduler)
pxCurrentTCB:       Points to first task's TCB
TCB->pxTopOfStack:  Points to task's initialized stack
Interrupts:         ENABLED (timer ticking at 1 KHz)
```

### During vPortStartFirstTask():
```
1. Load pxCurrentTCB address (0x79a0)
2. Load TCB pointer from pxCurrentTCB
3. Load SP from TCB->pxTopOfStack
   --> SP now points to task's stack!
4. Restore all 16 registers:
   --> ra = vTask1_FastBlink (e.g., 0x19c)
   --> a0 = NULL (task parameter)
   --> others = 0
5. SP += 64 (pop stack frame)
6. retirq executes:
   --> Interrupts still enabled
   --> PC = ra = vTask1_FastBlink
```

### After vPortStartFirstTask():
```
Stack:              First task's stack
SP:                 Task's stack pointer (in task's memory region)
PC:                 vTask1_FastBlink entry point (e.g., 0x19c)
Registers:          All set from task's initialized stack
Interrupts:         ENABLED
Timer:              Ticking every 1ms, calling xTaskIncrementTick()

We are now RUNNING IN THE FIRST TASK!
```

## Why This Works

### The Magic of retirq
PicoRV32's `retirq` instruction:
1. **Does NOT use standard RISC-V MRET** (no CSRs)
2. **Simply jumps to ra** (like `jalr x0, ra, 0`)
3. **Re-enables interrupts** (clears internal IRQ disable flag)

This is PERFECT for our use case! We load ra with the task entry point, and retirq jumps there with interrupts enabled.

### Stack Layout Compatibility
`pxPortInitialiseStack()` creates a stack frame that matches **exactly** what `irq_vec` expects:
- Same 16 registers in same order
- Same 64-byte frame size
- Same offset for ra (0) and a0 (4)

This means `vPortStartFirstTask()` can reuse the **exact same restore sequence** as `irq_vec`!

### Context Switching Integration
Once the first task is running:
1. Timer interrupts fire every 1ms
2. `irq_vec` saves context to current task's stack
3. `irq_handler()` may call `vTaskSwitchContext()`
4. `irq_vec` restores context from (possibly different) task's stack
5. `retirq` returns to (possibly different) task

The startup is just a special case: we restore from the first task's stack and "return" into it!

## Verification

### Build Results
```
Binary size: 14,120 bytes (14 KB)
Memory usage:
  Code:  14,120 bytes
  Data:      84 bytes
  BSS:   16,976 bytes (includes 16KB FreeRTOS heap)
  Total: 31,180 bytes
```

### Symbol Verification
```
000000fc T vPortStartFirstTask    ✓ Assembly function
00002cc8 T xPortStartScheduler    ✓ Port layer
000079a0 B pxCurrentTCB           ✓ FreeRTOS kernel variable
```

### Disassembly Verification
- ✓ vPortStartFirstTask loads from pxCurrentTCB (0x79a0)
- ✓ Restores all 16 caller-saved registers
- ✓ Ends with retirq instruction (0x0400000b)
- ✓ xPortStartScheduler calls vPortStartFirstTask with jal
- ✓ No return path after vPortStartFirstTask call

### Code Correctness
- ✓ Matches irq_vec restore sequence exactly
- ✓ Stack frame size matches (64 bytes)
- ✓ Register order matches pxPortInitialiseStack
- ✓ Interrupts enabled before first task starts
- ✓ Timer initialized before first task starts

## What Happens Next

### Expected Behavior on Hardware
1. **main()** prints startup message, creates 4 tasks, calls vTaskStartScheduler()
2. **xPortStartScheduler()** initializes timer, enables interrupts, calls vPortStartFirstTask()
3. **vPortStartFirstTask()** loads first task's context, executes retirq
4. **First task starts running** (e.g., vTask1_FastBlink)
5. **Timer interrupts fire** every 1ms
6. **Tasks switch** via xTaskIncrementTick() and vTaskSwitchContext()
7. **LEDs blink** at different rates (500ms, 1000ms, 2000ms)
8. **UART prints** status every 5 seconds

### What Could Go Wrong
1. **pxCurrentTCB is NULL**: FreeRTOS didn't initialize properly
2. **Stack pointer invalid**: Task stack corrupted or uninitialized
3. **Timer not firing**: Timer peripheral misconfigured
4. **Tasks don't switch**: Context switch code broken
5. **Crash on retirq**: Stack layout doesn't match expected format

## Next Steps (Tasks 6-7)

### Task 6: Hardware Testing
```bash
cd firmware
../tools/uploader/fw_upload_fast freertos_demo.bin
# Connect minicom to UART
minicom -D /dev/ttyUSB0 -b 115200
```

**Expected Output:**
```
========================================
FreeRTOS Multi-Task Demo for PicoRV32
========================================

FreeRTOS Configuration:
  CPU Clock:    0x02FAF080 Hz (50 MHz)
  Tick Rate:    0x000003E8 Hz (1 ms)
  Max Priority: 5
  Heap Size:    16384 bytes

Creating tasks...
  [OK] Task1: FastBlink created
  [OK] Task2: MediumBlink created
  [OK] Task3: SlowBlink created
  [OK] Task4: StatusReport created

Total tasks created: 4
Free heap after task creation: XXXXX bytes

Starting FreeRTOS scheduler...
Task1: Fast blinker started (500ms, LED0)
Task2: Medium blinker started (1000ms, LED1)
Task3: Slow blinker started (2000ms, LED2)
Task4: Status reporter started (5000ms)

--- System Status ---
Uptime cycles: 1
Tick count: 5000
Task count: 4
Free heap: XXXXX bytes
```

**Expected LED Behavior:**
- LED0: Blinks every 500ms (fast)
- LED1: Blinks every 1000ms (medium)
- LED2: Blinks every 2000ms (slow)

### Task 7: Debug and Tune
- Verify no stack overflows (check xPortGetFreeHeapSize())
- Verify tasks switch properly (different messages in UART)
- Adjust stack sizes if needed
- Tune priorities if needed
- Test with different workloads

## Completion Criteria

Task 5 is complete when:
- ✓ Code compiles without errors
- ✓ vPortStartFirstTask() present in binary
- ✓ xPortStartScheduler() calls vPortStartFirstTask()
- ✓ retirq instruction at end of vPortStartFirstTask()
- ✓ Stack layout matches irq_vec expectations

**STATUS: TASK 5 COMPLETE ✓**

Ready for hardware testing (Task 6).

**DO NOT COMMIT** until Tasks 6-7 are complete and tested on hardware!

---

## Summary

Task 5 implemented the critical scheduler startup mechanism by:
1. Adding `vPortStartFirstTask()` assembly function in startFRT.S
2. Modifying `xPortStartScheduler()` to call it
3. Leveraging PicoRV32's `retirq` to jump to first task
4. Reusing the exact same stack layout and restore sequence as irq_vec

This completes the FreeRTOS port's core functionality. All that remains is hardware testing (Task 6) and debugging (Task 7).

The implementation is elegant: we treat the first task startup as a special "interrupt return" into the task, using the same mechanism that will be used for all subsequent context switches.
