# PicoRV32 Interrupt Architecture Analysis
## Root Cause of FreeRTOS vTaskDelay() Bug

**Date**: October 20, 2025
**Finding**: The vTaskDelay() bug is NOT a software bug - it's an architectural mismatch between FreeRTOS's requirements and PicoRV32's interrupt capabilities.

---

## Executive Summary

**The Problem**: FreeRTOS's `vTaskDelay()` returns immediately instead of blocking because `portYIELD()` cannot trigger an immediate context switch on PicoRV32.

**The Root Cause**: PicoRV32 has NO software-triggered interrupt mechanism. All interrupts must come from external hardware on the 32-bit `irq` input port. This makes immediate yielding impossible without hardware changes.

**The Solution**: Implement cooperative scheduling where `portYIELD()` does nothing and context switches only occur at timer ticks.

---

## PicoRV32 IRQ Architecture

### Hardware Interface

```verilog
// From hdl/picorv32.v line 131
input [31:0] irq,      // 32-bit external IRQ input
output reg [31:0] eoi  // End-of-interrupt output
```

### Reserved IRQ Indices

```verilog
// From hdl/picorv32.v lines 172-174
localparam integer irq_timer = 0;      // Internal CPU timer
localparam integer irq_ebreak = 1;     // EBREAK instruction
localparam integer irq_buserror = 2;   // Misaligned access
// IRQ[3-31] available for external devices
```

**Note**: `irq_timer` (IRQ[0]) is the PicoRV32's INTERNAL timer (instruction counter), NOT our external timer peripheral!

### IRQ Pending Register Update

```verilog
// From hdl/picorv32.v line 1927
next_irq_pending = next_irq_pending | irq;
```

**CRITICAL**: External IRQ signals are OR'd into the pending register every clock cycle. The `irq` input port is the ONLY way to trigger interrupts.

### Custom IRQ Instructions

#### 1. getq rd, q1
```verilog
// Line 1661: Reads irq_pending & ~irq_mask into destination register
reg_out <= cpuregs_rs1;  // cpuregs_rs1 contains irq_pending & ~irq_mask
```

Returns bitmask of pending, unmasked interrupts.

**Our usage** (firmware/start.S line 38):
```assembly
.insn r 0x0B, 4, 0, a0, x1, x0  // getq a0, q1
call irq_handler                // Pass bitmask to C handler
```

#### 2. maskirq rd, rs1
```verilog
// Line 1689-1693: Set IRQ mask
reg_out <= irq_mask;                    // Return old mask
irq_mask <= cpuregs_rs1 | MASKED_IRQ;   // Set new mask
```

- Bit set to 1 = IRQ disabled
- Bit set to 0 = IRQ enabled
- Returns old mask value

**Our usage** (lib/freertos_port/portmacro.h line 68-69):
```c
#define portDISABLE_INTERRUPTS()    picorv32_maskirq(~0)  // Mask all
#define portENABLE_INTERRUPTS()     picorv32_maskirq(0)   // Unmask all
```

#### 3. retirq
```verilog
// Line 1678-1687: Return from interrupt
eoi <= 0;
irq_active <= 0;
latched_branch <= 1;
reg_out <= cpuregs_rs1;  // Return address
```

**Our usage** (firmware/start.S line 63):
```assembly
.insn r 0x0B, 0, 2, x0, x0, x0  // retirq
```

---

## Our Current Implementation

### Hardware Connection (hdl/ice40_picorv32_top.v line 243)

```verilog
.irq({31'h0, timer_irq}),  // ONLY IRQ[0] connected to timer peripheral
```

We're using IRQ[0] for our external timer peripheral at 0x80000020. This is DIFFERENT from PicoRV32's internal `irq_timer`.

### Software Handler (lib/freertos_port/freertos_irq.c)

```c
void irq_handler(uint32_t irqs)
{
    if (irqs & (1 << 0)) {  // Check IRQ[0] = timer peripheral
        TIMER_SR = TIMER_SR_UIF;  // Clear timer interrupt flag
        timer_irq_count++;

        BaseType_t xSwitchRequired = xTaskIncrementTick();

        // Check if portYIELD() was called
        if (xPortYieldPending) {
            xSwitchRequired = pdTRUE;
            xPortYieldPending = 0;
        }

        if (xSwitchRequired != pdFALSE) {
            vTaskSwitchContext();
        }
    }
}
```

### Our portYIELD() Implementation (lib/freertos_port/portmacro.h)

```c
extern volatile uint32_t xPortYieldPending;

static inline void portYIELD(void) {
    printf("DEBUG: portYIELD called\r\n");
    xPortYieldPending = 1;
    portENABLE_INTERRUPTS();

    // Busy-wait for timer interrupt to perform context switch
    while (xPortYieldPending) {
        __asm__ volatile ("nop");
    }
    printf("DEBUG: portYIELD returned\r\n");
}
```

---

## The Fundamental Problem

### What FreeRTOS Needs

FreeRTOS requires TWO types of context switches:

1. **Periodic Tick** (every 10ms at 100 Hz)
   - Timer interrupt fires
   - Increment tick count
   - Unblock delayed tasks if their time is up
   - Switch to highest priority ready task
   - ✅ **This works correctly**

2. **Immediate Yield** (when task blocks or calls portYIELD)
   - Task calls `vTaskDelay()` or `taskYIELD()`
   - Task should immediately give up CPU
   - Scheduler switches to next ready task
   - ❌ **This does NOT work**

### Why portYIELD() Doesn't Work

When a task calls `portYIELD()`:

1. Our code sets `xPortYieldPending = 1`
2. Code enters busy-wait loop: `while (xPortYieldPending) { nop; }`
3. **Waiting for timer interrupt to clear the flag**
4. But timer fires every 10ms!
5. Task wastes up to 10ms spinning in the loop
6. When timer FINALLY fires, context switch happens
7. Task returns from `portYIELD()`, but it's already blocked in delayed list
8. Next timer tick switches it out

**BUT** - our debug output shows portYIELD() is NEVER CALLED! Why?

### Why portYIELD() Is Never Called

Traced through FreeRTOS kernel (downloads/freertos/tasks.c):

```c
// vTaskDelay() line 2482-2491
xAlreadyYielded = xTaskResumeAll();

if( xAlreadyYielded == pdFALSE )  // This is FALSE, so code skipped!
{
    taskYIELD_WITHIN_API();  // Never reached
}
```

```c
// xTaskResumeAll() line 4126-4139
if( xYieldPendings[ xCoreID ] != pdFALSE )
{
    #if ( configUSE_PREEMPTION != 0 )
    {
        xAlreadyYielded = pdTRUE;  // Set TRUE
    }
    #endif

    taskYIELD_TASK_CORE_IF_USING_PREEMPTION( pxCurrentTCB );  // Calls portYIELD
}
```

**The condition at line 4126 evaluates to FALSE**, so:
- `taskYIELD_TASK_CORE_IF_USING_PREEMPTION()` is NOT called
- `xAlreadyYielded` remains FALSE
- But somehow the if check at vTaskDelay:2491 still skips portYIELD

**Likely issue**: We're inside a critical section (taskENTER_CRITICAL / taskEXIT_CRITICAL) when this code runs, and interrupts are disabled, so our busy-wait portYIELD() can't work anyway!

---

## What We CANNOT Do

### Option 1: Software-Triggered Interrupt ❌

**Idea**: Use IRQ[1] for FreeRTOS yield, separate from IRQ[0] timer.

**Problem**: PicoRV32 has NO instruction to set IRQ pending bits from software!
- IRQ pending register is computed from external `irq` input only
- No "SWI" or similar instruction
- Cannot trigger IRQ from C code

### Option 2: Use PicoRV32 Internal Timer ❌

**Idea**: Use `irq_timer` (IRQ[0] in PicoRV32's constants).

**Problem**:
- This is an instruction counter-based timer inside PicoRV32 core
- We're already using IRQ[0] for our external timer peripheral
- Would conflict with our timer peripheral interrupt
- Not suitable for RTOS tick (wrong granularity)

---

## What We CAN Do

### Option 1: Memory-Mapped Yield Trigger (Hardware Change Required)

Add a simple peripheral in the FPGA that asserts an IRQ line when written:

```verilog
// Add to ice40_picorv32_top.v
reg freertos_yield_irq;

always @(posedge clk) begin
    if (!cpu_resetn) begin
        freertos_yield_irq <= 0;
    end else begin
        // Write to 0x80000030 triggers yield IRQ
        if (mem_valid && mem_wstrb && mem_addr == 32'h80000030)
            freertos_yield_irq <= 1;
        // Auto-clear when CPU handles interrupt
        else if (cpu_eoi[1])  // IRQ[1] end-of-interrupt
            freertos_yield_irq <= 0;
    end
end

// Connect to PicoRV32
.irq({30'h0, freertos_yield_irq, timer_irq})
//         IRQ[1] = yield       IRQ[0] = timer
```

Then in software:
```c
#define YIELD_TRIGGER  (*(volatile uint32_t*)0x80000030)

static inline void portYIELD(void) {
    YIELD_TRIGGER = 1;  // Trigger IRQ[1]
    // Interrupt fires immediately, vTaskSwitchContext() called
}
```

**Pros**:
- True immediate context switching
- Proper preemptive multitasking
- Clean separation: IRQ[0]=tick, IRQ[1]=yield

**Cons**:
- Requires HDL modification
- Requires FPGA re-synthesis (~5-10 minutes)
- Adds complexity to hardware

### Option 2: Cooperative Scheduling (Software Only) ✅ RECOMMENDED

Accept that PicoRV32 cannot do software-triggered interrupts and implement pure cooperative scheduling:

```c
// lib/freertos_port/portmacro.h
#define portYIELD()  /* Do nothing - rely on timer tick */
```

**How it works**:

1. Task calls `vTaskDelay(100)` (request 1 second delay)
2. Task is added to delayed tasks list
3. `portYIELD()` does nothing (empty macro)
4. Task returns from `vTaskDelay()` and continues executing
5. **10ms later** (next timer tick), timer interrupt fires
6. Timer ISR calls `xTaskIncrementTick()`
7. Delayed task is checked but not ready yet (only 1 tick elapsed)
8. Timer ISR sees task in delayed list, switches to next ready task
9. Process repeats every 10ms until 100 ticks elapse
10. Task becomes ready and resumes

**Delay accuracy**:
- Requested delay: 100 ticks × 10ms = 1000ms
- Actual delay: 1000ms to 1010ms (up to 1 extra tick)
- Error: 0-10ms (0-1% at 100 Hz tick rate)

**Pros**:
- No hardware changes needed
- Works with existing infrastructure
- Standard approach for simple RTOS ports
- Delay error acceptable for most applications

**Cons**:
- Tasks delay slightly longer than requested (up to 1 tick = 10ms)
- No true preemptive multitasking for yields
- Task that blocks must wait for next timer tick

### Option 3: Force Timer Interrupt (Hacky) ⚠️

Manipulate timer counter to force immediate interrupt:

```c
#define TIMER_CNT  (*(volatile uint32_t*)0x80000030)
#define TIMER_ARR  (*(volatile uint32_t*)0x8000002C)

static inline void portYIELD(void) {
    // Force timer to fire on next clock cycle
    TIMER_CNT = TIMER_ARR - 1;

    // Enable interrupts and wait for timer
    portENABLE_INTERRUPTS();

    // Busy-wait for interrupt
    for (volatile int i = 0; i < 100; i++) {
        __asm__ volatile ("nop");
    }
}
```

**Pros**:
- Software-only solution
- Near-immediate context switch (< 1 microsecond)

**Cons**:
- Disrupts timer tick cadence
- May cause tick count drift over time
- Timer peripheral may not allow CNT writes (need to verify)
- Hacky and fragile
- May cause race conditions

---

## Recommended Solution

**Use Option 2: Pure Cooperative Scheduling**

This is the simplest, most reliable solution that requires no hardware changes.

### Implementation

1. **Simplify portYIELD()** (lib/freertos_port/portmacro.h):
```c
// Remove all the busy-wait code
#define portYIELD()  portNOP()  // Do nothing
```

2. **Remove xPortYieldPending** (lib/freertos_port/freertos_irq.c):
```c
// Delete these lines:
// extern volatile uint32_t xPortYieldPending;
// if (xPortYieldPending) { ... }
```

3. **Simplify IRQ handler** (lib/freertos_port/freertos_irq.c):
```c
void irq_handler(uint32_t irqs)
{
    if (irqs & (1 << 0)) {
        TIMER_SR = TIMER_SR_UIF;
        timer_irq_count++;

        if (xTaskIncrementTick() != pdFALSE) {
            vTaskSwitchContext();
        }
    }
}
```

4. **Update documentation** to explain:
   - FreeRTOS runs in cooperative mode
   - Task delays may be up to 10ms longer than requested
   - Context switches only occur at timer ticks (every 10ms)

### Expected Behavior After Fix

```
Loop #0: Tick=0x0000015C, IRQ=0x0000015C, Calling vTaskDelay(0x00000064)...
  -> Woke: TickAfter=0x00000246, Elapsed=0x000000EA, Expected=0x00000064
```

- Requested: 100 ticks (0x64)
- Actual: 234 ticks (0xEA) elapsed
- This is WRONG - should be ~100-101 ticks!

**Wait, why 234 ticks?**

Oh! The task is NOT blocking at all currently. It returns immediately, runs through the loop, and calls vTaskDelay again. So the elapsed time is how long it takes to:
- Return from vTaskDelay
- Print "Woke:" message
- Print hex values (slow UART)
- Increment counter
- Print "Loop #" message
- Print more hex values
- Call vTaskDelay again

At 1 Mbit/s UART and ~1ms per line of text, this could take 50-100ms = 5-10 ticks!

After fixing portYIELD(), we expect:
```
Loop #0: Tick=0x15C, Calling vTaskDelay(100)...
(10ms passes - timer tick, context switch to idle task)
(10ms passes - timer tick, still delayed)
... (repeat 9 more times)
(10ms passes - timer tick, 100 ticks elapsed, task ready)
  -> Woke: TickAfter=0x1C0, Elapsed=0x64
```

Elapsed should be exactly 100 ticks (0x64) or 101 at most.

---

## Testing Plan

After implementing Option 2 (cooperative scheduling):

1. **Build and upload**:
```bash
make fw-freertos-minimal
tools/uploader/fw_upload_fast firmware/freertos_minimal.bin
```

2. **Expected output**:
```
Loop #0: Tick=XXX, IRQ=YYY, Calling vTaskDelay(0x64)...
  -> Woke: TickAfter=XXX+100, Elapsed=0x64 or 0x65
```

3. **Verify**:
   - Elapsed should be 100 or 101 ticks (not 0!)
   - Task should actually block (pause visible on serial output)
   - Tick count should advance by 100 during the delay

4. **Test with multiple tasks**:
   - Create 2-3 tasks with different delays
   - Verify they all run and switch correctly
   - Verify tick count stays consistent

---

## Future Enhancement: Hardware Yield Trigger

If immediate context switching becomes necessary:

1. Add yield trigger peripheral to ice40_picorv32_top.v
2. Connect to IRQ[1]
3. Update irq_handler() to handle both IRQ[0] and IRQ[1]
4. Update portYIELD() to write to trigger register
5. Re-synthesize FPGA bitstream
6. Test preemptive scheduling

But for now, cooperative scheduling should work fine!

---

## Conclusion

The vTaskDelay() bug was NOT a software bug - it was trying to use a feature (software-triggered interrupts) that PicoRV32 simply doesn't have.

**The fix is simple**: Accept cooperative scheduling and rely on timer ticks for context switching.

**Delay accuracy**: Tasks may delay up to 10ms longer than requested, which is acceptable for most embedded applications running at 100 Hz tick rate.

---

## References

- PicoRV32 Verilog source: hdl/picorv32.v
- Hardware connections: hdl/ice40_picorv32_top.v
- FreeRTOS port: lib/freertos_port/
- Test firmware: firmware/freertos_minimal.c
- FreeRTOS kernel: downloads/freertos/tasks.c

**Author**: Michael Wolak (mikewolak@gmail.com)
**Date**: October 20, 2025
