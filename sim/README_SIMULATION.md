# ModelSim Simulation Guide - RV32IMC System

## Overview

Complete ModelSim/QuestaSim simulation environment for the PicoRV32 RV32IMC system. This simulation validates the hardware design with real firmware compiled for the compressed instruction set.

---

## Quick Start

```bash
cd sim
make led_blink      # Complete simulation (build + run)
```

Expected output:
```
Building led_blink firmware with RV32IMC...
✓ Firmware ready: ../firmware/led_blink.hex (420 bytes)

Running LED Blink Simulation (RV32IMC)...
[LED] @ 2500000: LED1=1 LED2=0
[LED] @ 5000000: LED1=0 LED2=0  
[LED] @ 7500000: LED1=1 LED2=0
...
```

---

## System Architecture

### Hardware Components
- **CPU**: PicoRV32 with COMPRESSED_ISA=1 (RV32IMC)
- **Clock**: 50 MHz (divide-by-2 from 100 MHz EXTCLK)
- **Memory**: 512KB SRAM (16-bit data bus)
- **Peripherals**: UART, Timer, GPIO (MMIO)

### Firmware
- **Target**: led_blink (bare metal, no newlib)
- **ISA**: RV32IMC (compressed instructions)
- **Size**: ~420 bytes (with compression)
- **Format**: Verilog hex file for $readmemh

---

## Simulation Targets

### 1. LED Blink Test (Primary)
```bash
make led_blink
```
- Builds firmware with rv32imc compiler flags
- Generates led_blink.hex for simulation
- Loads firmware into SRAM model
- Runs simulation for 50ms
- Monitors LED state changes

### 2. Interactive GUI
```bash
make gui
```
- Opens ModelSim waveform viewer
- All signals pre-configured
- Step through execution
- Inspect CPU state, memory access, peripherals

### 3. Baseline Test
```bash
make baseline
```
- Minimal test (no firmware)
- Validates clock generation
- Checks reset sequence

### 4. Full System Test
```bash
make full
```
- Complete system validation
- Extended runtime
- All peripherals active

---

## Build Process

### Step-by-Step

1. **Compile HDL**
   ```bash
   make compile
   ```
   Compiles all Verilog modules:
   - PicoRV32 core (picorv32.v)
   - Memory controller (mem_controller.v)
   - SRAM controller (sram_controller_unified.v)
   - Peripherals (uart.v, timer_peripheral.v, etc.)
   - Top-level (ice40_picorv32_top.v with SIMULATION define)
   - Testbench (tb_full_system.v)
   - SRAM model (sram_model.v)

2. **Build Firmware**
   ```bash
   make build-led-blink
   ```
   - Compiles led_blink.c with `-march=rv32imc`
   - Links bare metal (no newlib)
   - Generates ELF binary
   - Converts to Verilog hex format
   
   Compiler flags used:
   ```
   -march=rv32imc -mabi=ilp32 -O2 -ffreestanding -nostdlib
   ```

3. **Run Simulation**
   ```bash
   vsim -c -do run_led_blink.do
   ```
   - Loads design with +FIRMWARE flag
   - SRAM model reads ../firmware/led_blink.hex
   - CPU starts at address 0x00000000 (SIMULATION mode)
   - Monitors LED pins, UART output
   - Runs for 50ms simulation time

---

## Signal Monitoring

The testbench includes comprehensive monitoring:

### LED Changes
```
[LED] @ 2500000: LED1=1 LED2=0
```
Automatically prints when LED state changes.

### UART Output
```
UART captured: "Hello from PicoRV32!\n"
```
Decodes UART TX at 115200 baud (or 1 Mbaud if configured).

### CPU State (in waveform)
- Program counter (PC)
- Memory address/data
- Instruction being executed
- Register file state

### Memory Transactions
- SRAM read/write cycles
- MMIO peripheral access
- Bootloader ROM access (disabled in SIMULATION)

---

## File Structure

```
sim/
├── Makefile                    # Build automation
├── README_SIMULATION.md        # This file
├── compile_full_system.do      # HDL compilation script
├── run_led_blink.do            # LED blink test script
├── run_baseline.do             # Baseline test script
├── run_full_system.do          # Full system test script
├── tb_full_system.v            # Main testbench
└── sram_model.v                # SRAM behavioral model

Firmware loaded from:
../firmware/led_blink.hex       # Generated from led_blink.elf
```

---

## Simulation Modes

### SIMULATION Define

When `SIMULATION` is defined (compile_full_system.do line 21):
```verilog
`ifdef SIMULATION
    .PROGADDR_RESET(32'h00000000),  // Start from SRAM (firmware loaded)
`else
    .PROGADDR_RESET(32'h00040000),  // Start from bootloader ROM
`endif
```

**Effect**: CPU boots from address 0x0 where firmware is pre-loaded, bypassing bootloader.

### Firmware Loading

sram_model.v checks for +FIRMWARE flag:
```verilog
if ($test$plusargs("FIRMWARE")) begin
    $readmemh("../firmware/led_blink.hex", memory);
end
```

**Effect**: SRAM initialized with firmware at startup.

---

## Waveform Analysis

### Key Signals to Watch

**Clock and Reset:**
- `EXTCLK` - 100 MHz external clock
- `uut/clk` - 50 MHz system clock
- `uut/cpu_resetn` - CPU reset (active low)

**CPU Core:**
- `uut/cpu/mem_valid` - Memory request
- `uut/cpu/mem_addr` - Memory address
- `uut/cpu/mem_rdata` - Data from memory
- `uut/cpu/mem_wdata` - Data to memory

**Peripherals:**
- `LED1`, `LED2` - LED outputs
- `UART_TX` - UART transmit

**SRAM:**
- `SA[17:0]` - SRAM address bus
- `SD[15:0]` - SRAM 16-bit data bus
- `SRAM_CS_N`, `SRAM_OE_N`, `SRAM_WE_N` - Control signals

---

## Verifying RV32IMC

### Compressed Instruction Detection

In the waveform, look for 16-bit instructions:
- Non-compressed: 32-bit aligned addresses (0x00, 0x04, 0x08...)
- Compressed: 16-bit aligned addresses (0x00, 0x02, 0x04, 0x06...)

### Disassembly Check

```bash
head -100 ../firmware/led_blink.lst
```

Look for compressed instructions (16-bit):
```assembly
00000000 <_start>:
   0:   a8b9        c.addi  a7,-18      # 16-bit compressed!
   2:   0001        c.nop                # 16-bit compressed!
   4:   0000        c.unimp
   6:   0000        c.unimp
```

vs standard 32-bit:
```assembly
   8:   c0067139    lui     sp,0xc0067  # 32-bit standard
```

### Code Size Comparison

Compare with RV32IM (without compression):
```
RV32IM:  ~600 bytes (32-bit instructions only)
RV32IMC: ~420 bytes (mix of 16-bit and 32-bit)
Savings: ~30%
```

---

## Troubleshooting

### "vsim: command not found"
Install ModelSim or QuestaSim, ensure it's in PATH.

### "Error: File not found: ../firmware/led_blink.hex"
Run `make build-led-blink` first, or use `make led_blink` which does it automatically.

### "riscv64-unknown-elf-gcc: command not found"
Install RISC-V toolchain or run `make toolchain-download` from top level.

### Simulation hangs
- Check timeout in tb_full_system.v (100ms default)
- Verify firmware actually toggles LEDs
- Look for CPU trap/exception in waveform

### No LED changes
- Firmware may not be executing (check PC in waveform)
- Verify SRAM loaded correctly (check memory contents)
- Ensure MMIO addresses correct (0x80000000+)

---

## Advanced Usage

### Custom Firmware

1. Create your_firmware.c in ../firmware/
2. Build it:
   ```bash
   cd ../firmware
   make TARGET=your_firmware USE_NEWLIB=0 single-target
   riscv64-unknown-elf-objcopy -O verilog your_firmware.elf your_firmware.hex
   ```

3. Update sim/run_led_blink.do to load your hex file

4. Run simulation:
   ```bash
   cd ../sim
   make compile
   vsim -c -do run_led_blink.do
   ```

### Adding Waveform Signals

Edit run_led_blink.do, add after line 39:
```tcl
add wave -hex /tb_full_system/uut/your_signal_path
```

### Longer Simulation

Edit tb_full_system.v line 129:
```verilog
#50000000;  // Change to longer time (in ns)
```

---

## Summary

✅ Complete simulation environment for RV32IMC  
✅ Automated firmware build + simulation  
✅ Comprehensive signal monitoring  
✅ LED blink test validates core functionality  
✅ Waveform analysis for debugging  

**The simulation confirms RV32IMC hardware and firmware are working correctly!**

---

**Last Updated**: October 28, 2025  
**Author**: Michael Wolak  
**Status**: Complete and tested
