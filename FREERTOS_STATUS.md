# FreeRTOS Integration Status

**Status**: Work in Progress - NOT READY FOR USE

## What's Complete

1. **Kconfig Integration**
   - Added FreeRTOS menu to Kconfig
   - Minimal essential configuration options:
     - CPU clock (50 MHz default)
     - Tick rate (1000 Hz default)
     - Max priorities (5 default)
     - Minimal stack size (128 words default)
     - Total heap size (16 KB default)
   - FreeRTOS disabled by default
   - Automatically selects newlib dependency

2. **Configuration Header**
   - `lib/freertos_config/FreeRTOSConfig.h` created
   - Uses CONFIG_* variables from Kconfig
   - Requires CONFIG_* to be passed as -D compiler flags
   - Minimal configuration for proof-of-concept

3. **Port Layer (Partial)**
   - `lib/freertos_port/portmacro.h` - Type definitions and PicoRV32 IRQ macros
   - `lib/freertos_port/port.c` - Basic port functions (incomplete)
   - Uses PicoRV32 custom IRQ instructions (maskirq, getirq)

4. **Test Firmware**
   - `firmware/freertos_minimal.c` - Minimal test (no scheduler yet)

## What's NOT Complete

### Critical Missing Components

1. **Context Switching**
   - No portASM.S implementation yet
   - Need to integrate with existing `start.S` IRQ vector
   - Must use PicoRV32 `retirq` instruction

2. **Timer Integration**
   - No tick interrupt implementation
   - Need to integrate FreeRTOS tick handler into existing `irq_handler`
   - Timer setup code needed

3. **Scheduler Start**
   - `xPortStartScheduler()` incomplete
   - Need to load first task context and jump to it

4. **Build System**
   - No Makefile integration yet
   - Need to:
     - Pass CONFIG_* as -D flags
     - Link FreeRTOS kernel sources
     - Link port layer
     - Add FreeRTOS include paths

5. **Testing**
   - NO BUILD TESTING DONE YET
   - NO COMMITS MADE (as required)

## PicoRV32-Specific Requirements

The port MUST use PicoRV32's custom interrupt system:

### Custom Instructions
```assembly
.insn r 0x0B, 6, 3, rd, rs1, x0    # maskirq rd, rs1 (set IRQ mask)
.insn r 0x0B, 4, 0, rd, x1, x0      # getq rd, q1 (get IRQ status)
.insn r 0x0B, 0, 2, x0, x0, x0      # retirq (return from interrupt)
```

### IRQ Vector
- Located at 0x10 (PROGADDR_IRQ)
- Defined in `start.S`
- Saves all caller-saved registers
- Calls `irq_handler()` in C
- Uses `retirq` to return

### IRQ Control
- Enable: `maskirq(0)` - clear mask
- Disable: `maskirq(~0)` - set all bits

## Next Steps

1. Implement full context switching in assembly
2. Integrate timer tick interrupt
3. Complete scheduler startup
4. Update build system to:
   - Download FreeRTOS kernel
   - Pass CONFIG_* as -D flags
   - Build and link everything
5. Create actual multi-task demo
6. **TEST BUILD BEFORE ANY COMMITS**

## Design Decisions

- Kept configuration minimal to reduce complexity
- Disabled by default in Kconfig
- Using heap_4 allocator (standard choice)
- 16KB default heap (conservative for 512KB SRAM)
- No static allocation to keep it simple
- No trace/stats initially

## Reference Files

Examples of working PicoRV32 interrupt handling:
- `firmware/start.S` - IRQ vector and `irq_handler` integration
- `firmware/timer_clock.c` - Timer interrupt example
- `firmware/hexedit_fast.c` - IRQ enable/disable examples

## DO NOT USE YET

This is incomplete work in progress. Do not enable FreeRTOS in .config or attempt to build freertos_minimal until the remaining components are implemented and tested.
