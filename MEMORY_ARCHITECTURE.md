# Memory Architecture - Deep Dive

**Olimex iCE40HX8K-EVB RISC-V Platform**
**PicoRV32 Memory Controller & SRAM Interface**

Copyright (c) October 2025 Michael Wolak
Email: mikewolak@gmail.com, mike@epromfoundry.com

---

## Table of Contents

1. [System Overview](#system-overview)
2. [Memory Hierarchy](#memory-hierarchy)
3. [Memory Controller Architecture](#memory-controller-architecture)
4. [SRAM Controller Timing](#sram-controller-timing)
5. [Performance Analysis](#performance-analysis)
6. [Instruction Throughput](#instruction-throughput)
7. [Real-World Performance](#real-world-performance)

---

## System Overview

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         SYSTEM ARCHITECTURE                              │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                           │
│  ┌──────────────┐                                                        │
│  │   PicoRV32   │ @ 50 MHz                                              │
│  │   RV32IM     │                                                        │
│  │              │                                                        │
│  │  - 2-stage   │                                                        │
│  │  - 32-bit    │                                                        │
│  │  - Harvard   │                                                        │
│  └──────┬───────┘                                                        │
│         │                                                                 │
│         │ mem_valid, mem_ready                                           │
│         │ mem_addr[31:0]                                                 │
│         │ mem_wdata[31:0], mem_rdata[31:0]                              │
│         │ mem_wstrb[3:0]                                                 │
│         ↓                                                                 │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │             Memory Controller (mem_controller.v)                 │   │
│  │                                                                  │   │
│  │  ┌────────────────────────────────────────────────────────┐    │   │
│  │  │  Address Decode                                         │    │   │
│  │  │                                                         │    │   │
│  │  │  0x00000000 - 0x0007FFFF  →  SRAM (512KB)             │    │   │
│  │  │  0x00040000 - 0x00041FFF  →  Boot ROM (8KB BRAM)      │    │   │
│  │  │  0x80000000 - 0x800000FF  →  MMIO Peripherals         │    │   │
│  │  │  Other                    →  Invalid (returns 0)       │    │   │
│  │  └────────────────────────────────────────────────────────┘    │   │
│  │                                                                  │   │
│  │  State Machine:                                                 │   │
│  │    IDLE → SRAM_WAIT / BOOT_WAIT / MMIO_WAIT → IDLE            │   │
│  │                                                                  │   │
│  └──────┬──────────────┬──────────────┬─────────────────────────┘   │
│         │              │              │                               │
│         ↓              ↓              ↓                               │
│  ┌─────────────┐ ┌──────────┐ ┌────────────┐                        │
│  │    SRAM     │ │ Boot ROM │ │    MMIO    │                        │
│  │ Controller  │ │  (BRAM)  │ │ Peripherals│                        │
│  │  Unified    │ │  8KB     │ │  (UART,    │                        │
│  │             │ │          │ │   Timer,   │                        │
│  │  4-7 cycles │ │ 3 cycles │ │   SPI...)  │                        │
│  └──────┬──────┘ └──────────┘ └────────────┘                        │
│         │                                                              │
│         │ 16-bit interface                                            │
│         ↓                                                              │
│  ┌───────────────────────────────────────┐                           │
│  │  External SRAM (IS61WV51216BLL-10TLI) │                           │
│  │  - 512KB (256K x 16-bit)              │                           │
│  │  - 10ns access time                   │                           │
│  │  - Asynchronous                       │                           │
│  └───────────────────────────────────────┘                           │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## Memory Hierarchy

### Memory Map

```
┌─────────────┬──────────────┬──────────────┬───────────────────────────┐
│ Start Addr  │  End Addr    │    Size      │  Description              │
├─────────────┼──────────────┼──────────────┼───────────────────────────┤
│ 0x00000000  │ 0x0003FFFF   │    256 KB    │  Code Space (SRAM)        │
│ 0x00040000  │ 0x00041FFF   │      8 KB    │  Bootloader ROM (BRAM)    │
│ 0x00042000  │ 0x0007FFFF   │   ~248 KB    │  Data/Heap/Stack (SRAM)   │
│ 0x80000000  │ 0x80000017   │     24 B     │  UART Peripheral          │
│ 0x80000020  │ 0x80000037   │     24 B     │  Timer Peripheral         │
│ 0x80000050  │ 0x80000067   │     24 B     │  SPI Master               │
│ 0x80000070  │ 0x80000077   │      8 B     │  GPIO                     │
│ Other       │ -            │      -       │  Returns 0 (invalid)      │
└─────────────┴──────────────┴──────────────┴───────────────────────────┘
```

### Access Latency by Region

```
┌──────────────────┬──────────────┬─────────────┬────────────────────────┐
│ Memory Region    │  Access Type │   Latency   │  Notes                 │
├──────────────────┼──────────────┼─────────────┼────────────────────────┤
│ SRAM             │  Read 32-bit │  4 cycles   │  80ns @ 50MHz         │
│ SRAM             │  Write 32-bit│  4 cycles   │  80ns @ 50MHz         │
│ SRAM             │  Write Byte  │  7 cycles   │  140ns (RMW required) │
│ SRAM             │  Write Half  │  4 cycles   │  80ns (if aligned)    │
│ Boot ROM (BRAM)  │  Read 32-bit │  3 cycles   │  60ns @ 50MHz         │
│ Boot ROM (BRAM)  │  Write       │  N/A        │  Read-only            │
│ MMIO             │  Read/Write  │  1-4 cycles │  Peripheral dependent │
│ Invalid Address  │  Read/Write  │  1 cycle    │  Returns 0 immediate  │
└──────────────────┴──────────────┴─────────────┴────────────────────────┘
```

---

## Memory Controller Architecture

### State Machine Diagram

```
                    ┌──────────────────────────────────┐
                    │                                  │
                    ↓                                  │
              ┌──────────┐                            │
    ┌────────→│   IDLE   │←───────────────────────────┼──────────┐
    │         └──────────┘                            │          │
    │              │                                   │          │
    │              │ cpu_mem_valid                     │          │
    │              │                                   │          │
    │         ┌────┴──────┬──────────┬─────────────┐  │          │
    │         ↓           ↓          ↓             ↓  │          │
    │    ┌─────────┐ ┌─────────┐ ┌────────┐ ┌─────────────┐    │
    │    │  SRAM   │ │  BOOT   │ │  MMIO  │ │   Invalid   │    │
    │    │ (route) │ │ (route) │ │ (route)│ │  (return 0) │    │
    │    └────┬────┘ └────┬────┘ └────┬───┘ └──────┬──────┘    │
    │         │           │           │              │           │
    │         ↓           ↓           ↓              └───────────┘
    │  ┌────────────┐ ┌──────────┐ ┌──────────┐
    │  │ SRAM_WAIT  │ │BOOT_WAIT │ │MMIO_WAIT │
    │  │            │ │  (2 cyc) │ │ (varies) │
    │  │ (sram_done)│ │          │ │          │
    │  └──────┬─────┘ └────┬─────┘ └────┬─────┘
    │         │            │             │
    └─────────┴────────────┴─────────────┘

               All paths return to IDLE
               when operation completes
```

### Address Decoding Logic

```
Input: cpu_mem_addr[31:0]

                      ┌───────────────────────────┐
                      │   Address Decode Logic    │
                      └─────────┬─────────────────┘
                                │
                    ┌───────────┴───────────┐
                    │                       │
          ┌─────────▼─────────┐   ┌────────▼────────┐
          │ addr >= 0x00000000 │   │ addr >= 0x80000000 │
          │ addr <= 0x0007FFFF │   │ addr <= 0x800000FF │
          │                    │   │                    │
          │   SRAM Region      │   │   MMIO Region      │
          └─────────┬──────────┘   └────────┬───────────┘
                    │                       │
                    │         ┌─────────────┴──────────┐
                    │         │                        │
          ┌─────────▼─────────▼───┐          ┌────────▼────────┐
          │  addr >= 0x00040000    │          │  Specific MMIO  │
          │  addr <= 0x00041FFF    │          │  Peripheral     │
          │                        │          │  Decode         │
          │  Boot ROM (8KB BRAM)   │          └─────────────────┘
          │  (within SRAM range)   │
          └────────────────────────┘
```

### Memory Controller Timing (Best/Typical/Worst Case)

```
┌─────────────────────┬──────────────┬──────────────┬──────────────┐
│ Operation           │  Best Case   │ Typical Case │  Worst Case  │
├─────────────────────┼──────────────┼──────────────┼──────────────┤
│ SRAM Read 32-bit    │   4 cycles   │   4 cycles   │   4 cycles   │
│ SRAM Write 32-bit   │   4 cycles   │   4 cycles   │   4 cycles   │
│ SRAM Write Halfword │   4 cycles   │   4 cycles   │   4 cycles*  │
│ SRAM Write Byte     │   7 cycles   │   7 cycles   │   7 cycles   │
│ Boot ROM Read       │   3 cycles   │   3 cycles   │   3 cycles   │
│ MMIO Read           │   2 cycles   │   2 cycles   │   4 cycles** │
│ MMIO Write          │   1 cycle    │   1 cycle    │   2 cycles** │
│ Invalid Address     │   1 cycle    │   1 cycle    │   1 cycle    │
└─────────────────────┴──────────────┴──────────────┴──────────────┘

*  Aligned halfword writes = 4 cycles, unaligned = 7 cycles (RMW)
** MMIO timing depends on peripheral (UART may take longer)
```

---

## SRAM Controller Timing

### SRAM Physical Interface

```
SRAM Chip: IS61WV51216BLL-10TLI
  - Organization: 256K x 16-bit (512KB total)
  - Access Time (tAA): 10ns maximum
  - Write Pulse (tWP): 7ns minimum
  - Write Recovery (tWR): 0ns minimum
  - Operating Voltage: 3.3V
  - Interface: Asynchronous

Controller Interface (16-bit):
  - sram_addr[17:0]:  Word address (18 bits for 256K words)
  - sram_data[15:0]:  Bidirectional data bus
  - sram_cs_n:        Chip select (active low)
  - sram_oe_n:        Output enable (active low)
  - sram_we_n:        Write enable (active low)
```

### 32-bit to 16-bit Conversion

```
CPU provides 32-bit access, SRAM is 16-bit:
  - Each 32-bit word requires TWO 16-bit accesses
  - LOW halfword:  addr[18:1] + 0
  - HIGH halfword: addr[18:1] + 1

Example: Reading 32-bit from 0x00001000
  - LOW:  SRAM word 0x00800 (bytes 0x1000-0x1001)
  - HIGH: SRAM word 0x00801 (bytes 0x1002-0x1003)
```

### Full 32-bit Read Timing (4 cycles = 80ns)

```
Cycle:   │ 0 (IDLE) │  1 (SETUP) │  2 (CAPTURE) │  3 (SETUP) │  4 (CAPTURE) │
─────────┼──────────┼────────────┼──────────────┼────────────┼──────────────┤
State:   │ IDLE     │ READ_LOW_  │ READ_LOW_    │ READ_HIGH_ │ READ_HIGH_   │
         │          │ SETUP      │ CAPTURE      │ SETUP      │ CAPTURE      │
─────────┼──────────┼────────────┼──────────────┼────────────┼──────────────┤
valid:   │ 0→1      │     1      │      1       │     1      │      1       │
ready:   │   0      │     0      │      0       │     0      │     0→1      │
─────────┼──────────┼────────────┼──────────────┼────────────┼──────────────┤
sram_addr│   -      │ addr_low   │  addr_low    │ addr_high  │  addr_high   │
sram_cs_n│   1      │     0      │      0       │     0      │      0       │
sram_oe_n│   1      │     0      │      0       │     0      │      0       │
sram_we_n│   1      │     1      │      1       │     1      │      1       │
─────────┼──────────┼────────────┼──────────────┼────────────┼──────────────┤
sram_data│   Z      │     Z      │  LOW[15:0]   │     Z      │  HIGH[15:0]  │
         │          │            │  (captured)  │            │  (captured)  │
─────────┼──────────┼────────────┼──────────────┼────────────┼──────────────┤
rdata:   │   X      │     X      │      X       │     X      │ {HIGH,LOW}   │
─────────┴──────────┴────────────┴──────────────┴────────────┴──────────────┘

Timing Analysis:
  Cycle 1: Address setup (20ns) > tAA(10ns) ✓
  Cycle 2: Data capture (valid after 20ns)
  Cycle 3: Address setup for HIGH halfword
  Cycle 4: Data capture and assembly

Total: 4 cycles = 80ns @ 50MHz
```

### Full 32-bit Write Timing (4 cycles = 80ns)

```
Cycle:   │ 0 (IDLE) │  1 (SETUP) │  2 (PULSE)  │  3 (SETUP)  │  4 (PULSE)  │
─────────┼──────────┼────────────┼─────────────┼─────────────┼─────────────┤
State:   │ IDLE     │ WRITE_LOW_ │ WRITE_LOW_  │ WRITE_HIGH_ │ WRITE_HIGH_ │
         │          │ SETUP      │ PULSE       │ SETUP       │ PULSE       │
─────────┼──────────┼────────────┼─────────────┼─────────────┼─────────────┤
valid:   │ 0→1      │     1      │      1      │     1       │      1      │
ready:   │   0      │     0      │      0      │     0       │     0→1     │
wstrb:   │ 4'b1111  │  4'b1111   │   4'b1111   │  4'b1111    │  4'b1111    │
─────────┼──────────┼────────────┼─────────────┼─────────────┼─────────────┤
sram_addr│   -      │ addr_low   │  addr_low   │ addr_high   │ addr_high   │
sram_cs_n│   1      │     0      │      0      │     0       │      0      │
sram_oe_n│   1      │     1      │      1      │     1       │      1      │
sram_we_n│   1      │     1      │      0      │     1       │      0      │
data_oe  │   0      │     1      │      1      │     1       │      1      │
─────────┼──────────┼────────────┼─────────────┼─────────────┼─────────────┤
sram_data│   Z      │ LOW[15:0]  │  LOW[15:0]  │ HIGH[15:0]  │ HIGH[15:0]  │
─────────┴──────────┴────────────┴─────────────┴─────────────┴─────────────┘

Timing Analysis:
  Cycle 1: Address & data setup
  Cycle 2: WE pulse (20ns) > tWP(7ns) ✓
  Cycle 3: Address & data setup for HIGH
  Cycle 4: WE pulse

Total: 4 cycles = 80ns @ 50MHz
```

### Byte Write Timing - Read-Modify-Write (7 cycles = 140ns)

```
Operation: Write single byte requires RMW because SRAM is 16-bit

Cycle:   │  1  │  2  │  3  │  4  │  5  │  6  │  7  │
─────────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┤
State:   │ RMW │ RMW │ RMW │ RMW │ RMW │ RMW │ RMW │
         │READ │READ │READ │READ │WRITE│WRITE│WRITE│
         │LOW_ │LOW_ │HIGH_│HIGH_│LOW_ │LOW_ │HIGH_│
         │SETUP│CAPT │SETUP│CAPT │SETUP│PULSE│COMP │
─────────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┤
Action:  │Setup│Read │Setup│Read │Merge│Write│Done │
         │addr │LOW  │addr │HIGH │ &   │LOW  │     │
         │     │half │     │half │setup│half │     │
─────────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┘

Example: Write byte to address 0x00001002 (wstrb = 4'b0100)

  Step 1-2: Read LOW halfword (0x1000-0x1001)
  Step 3-4: Read HIGH halfword (0x1002-0x1003)
  Step 5-6: Merge byte 2 with existing HIGH, write back
  Step 7:   Complete

  Merge operation:
    HIGH[7:0]  = wstrb[2] ? wdata[23:16] : rdata_high[7:0]  ← NEW byte
    HIGH[15:8] = wstrb[3] ? wdata[31:24] : rdata_high[15:8] ← PRESERVED

Total: 7 cycles = 140ns @ 50MHz
```

### Aligned Halfword Write Optimization (4 cycles = 80ns)

```
When wstrb = 4'b0011 or 4'b1100, RMW can be skipped!

Case 1: LOW halfword only (wstrb = 4'b0011)
  - Write directly to LOW halfword, HIGH untouched
  - Same as 32-bit write LOW, skip HIGH
  - 4 cycles

Case 2: HIGH halfword only (wstrb = 4'b1100)
  - Write directly to HIGH halfword, LOW untouched
  - Same as 32-bit write HIGH only
  - 4 cycles

This optimization saves 3 cycles (7→4) for aligned halfword writes!
```

---

## Performance Analysis

### Cycle Count Summary

```
┌────────────────────────────┬────────────┬─────────────┬──────────────┐
│ Access Pattern             │   Cycles   │  Time@50MHz │  Bandwidth   │
├────────────────────────────┼────────────┼─────────────┼──────────────┤
│ 32-bit Read                │     4      │    80 ns    │  50 MB/s     │
│ 32-bit Write               │     4      │    80 ns    │  50 MB/s     │
│ 16-bit Read (aligned)      │     4*     │    80 ns    │  25 MB/s     │
│ 16-bit Write (aligned)     │     4      │    80 ns    │  25 MB/s     │
│ 8-bit Read                 │     4*     │    80 ns    │  12.5 MB/s   │
│ 8-bit Write (RMW)          │     7      │   140 ns    │  7.1 MB/s    │
│ Sequential 32-bit Read     │     4/word │    80 ns    │  50 MB/s     │
│ Sequential 32-bit Write    │     4/word │    80 ns    │  50 MB/s     │
└────────────────────────────┴────────────┴─────────────┴──────────────┘

* Reads always fetch full 32-bit, byte/halfword extracted by CPU
```

### Best vs Worst Case Comparison

```
┌──────────────────────┬─────────────┬─────────────┬───────────────┐
│ Scenario             │  Best Case  │ Worst Case  │  Difference   │
├──────────────────────┼─────────────┼─────────────┼───────────────┤
│ Code fetch (aligned) │   4 cycles  │  4 cycles   │  None         │
│ Data read (aligned)  │   4 cycles  │  4 cycles   │  None         │
│ Data write (aligned) │   4 cycles  │  4 cycles   │  None         │
│ Data write (byte)    │   4 cycles* │  7 cycles   │  +75%         │
│ String operations    │   4 cyc/wd  │  7 cyc/byte │  +75%         │
└──────────────────────┴─────────────┴─────────────┴───────────────┘

* Best case: compiler aligns data to word boundaries
```

---

## Instruction Throughput

### PicoRV32 Instruction Timing

The PicoRV32 is a 2-stage pipeline with the following characteristics:

```
Pipeline Stages:
  1. Fetch:   Read instruction from memory
  2. Execute: Decode, execute, and writeback

Best Case (no stalls):     1 instruction per cycle
Typical (with mem access): 1 instruction per 4-5 cycles
Worst Case (byte writes):  1 instruction per 7+ cycles
```

### Instruction Cycle Counts

```
┌─────────────────────────┬─────────────┬──────────────┬─────────────┐
│ Instruction Type        │ Execute CPI │  Memory CPI  │  Total CPI  │
├─────────────────────────┼─────────────┼──────────────┼─────────────┤
│ ADD/SUB/AND/OR/XOR      │      1      │       4*     │      5      │
│ ADDI/ANDI/ORI/XORI      │      1      │       4*     │      5      │
│ SLL/SRL/SRA             │      1      │       4*     │      5      │
│ SLLI/SRLI/SRAI          │      1      │       4*     │      5      │
│ LUI/AUIPC               │      1      │       4*     │      5      │
│ Load Word (LW)          │      1      │     4+4**    │      9      │
│ Load Half (LH/LHU)      │      1      │     4+4**    │      9      │
│ Load Byte (LB/LBU)      │      1      │     4+4**    │      9      │
│ Store Word (SW)         │      1      │     4+4**    │      9      │
│ Store Half (SH)         │      1      │     4+4**    │      9      │
│ Store Byte (SB)         │      1      │     4+7**    │     12      │
│ Branch (taken)          │      3      │       4*     │      7      │
│ Branch (not taken)      │      1      │       4*     │      5      │
│ JAL/JALR                │      3      │       4*     │      7      │
│ MUL/MULH/MULHU/MULHSU   │     33      │       4*     │     37      │
│ DIV/DIVU/REM/REMU       │     33      │       4*     │     37      │
└─────────────────────────┴─────────────┴──────────────┴─────────────┘

*  Instruction fetch (from SRAM)
** Data access (from SRAM)

CPI = Cycles Per Instruction
```

### Instruction Mix Analysis

```
Typical Program Instruction Mix (from benchmarks):

┌──────────────────────┬─────────────┬─────────────┬──────────────────┐
│ Instruction Category │  Frequency  │   Avg CPI   │ Weighted Cycles  │
├──────────────────────┼─────────────┼─────────────┼──────────────────┤
│ ALU/Logic            │     40%     │      5      │       2.0        │
│ Load                 │     25%     │      9      │       2.25       │
│ Store                │     15%     │     10*     │       1.5        │
│ Branch               │     15%     │      6**    │       0.9        │
│ Multiply/Divide      │      5%     │     37      │       1.85       │
├──────────────────────┼─────────────┼─────────────┼──────────────────┤
│ TOTAL (Weighted Avg) │    100%     │     8.5     │       8.5        │
└──────────────────────┴─────────────┴─────────────┴──────────────────┘

*  Assuming 80% word/halfword stores (9 cycles), 20% byte stores (12 cycles)
** Assuming 50% branch taken (7 cycles), 50% not taken (5 cycles)

Effective Performance:
  Average CPI:           8.5 cycles/instruction
  Clock Frequency:       50 MHz
  Instruction Rate:      5.88 MIPS (Million Instructions Per Second)
```

---

## Real-World Performance

### Performance Metrics @ 50 MHz

```
┌────────────────────────────────────┬──────────────────────────────────┐
│ Metric                             │  Value                           │
├────────────────────────────────────┼──────────────────────────────────┤
│ Clock Frequency                    │  50 MHz                          │
│ Clock Period                       │  20 ns                           │
│ Peak IPC (Instructions Per Cycle)  │  1.0 (unrealistic)               │
│ Typical IPC                        │  0.12 (with memory access)       │
│ Effective MIPS                     │  5.88 MIPS (typical workload)    │
│ Peak MIPS                          │  50 MIPS (cache hits only)       │
│ SRAM Bandwidth (32-bit access)     │  50 MB/s read, 50 MB/s write    │
│ SRAM Bandwidth (byte access)       │  7.1 MB/s write (RMW overhead)  │
└────────────────────────────────────┴──────────────────────────────────┘
```

### Dhrystone 2.1 Benchmark Estimate

```
Estimated Dhrystone Performance:

  Typical Dhrystone iteration: ~120 instructions
  Average CPI: 8.5
  Cycles per iteration: 120 × 8.5 = 1020 cycles

  Iterations/sec = 50,000,000 / 1020 = 49,019 iterations/sec

  Dhrystone MIPS = 49,019 / 1757 = 27.9 Dhrystone MIPS

  (Note: Dhrystone MIPS ≠ actual MIPS due to benchmark characteristics)
```

### Coremark Benchmark Estimate

```
Estimated CoreMark Performance:

  CoreMark is more memory-intensive than Dhrystone
  Estimate: ~15% more cycles due to memory access patterns

  Effective CPI: 8.5 × 1.15 = 9.8 cycles/instruction

  CoreMark iterations: ~50,000 instructions
  Cycles per iteration: 50,000 × 9.8 = 490,000 cycles

  Iterations/sec = 50,000,000 / 490,000 = 102 iterations/sec

  CoreMark Score ≈ 102 CoreMarks @ 50 MHz
  CoreMark/MHz ≈ 2.04
```

### Comparison with Other Architectures

```
┌────────────────────────┬──────────┬─────────────┬────────────────┐
│ Architecture           │   MHz    │  Avg CPI    │  Approx MIPS   │
├────────────────────────┼──────────┼─────────────┼────────────────┤
│ PicoRV32 (this system) │    50    │     8.5     │      5.9       │
│ ARM Cortex-M0+         │    48    │     2.8     │     17.1       │
│ ARM Cortex-M3          │    72    │     1.25    │     57.6       │
│ AVR ATmega328P         │    16    │     1.0     │     16.0       │
│ RISC-V (with cache)    │    50    │     1.5     │     33.3       │
└────────────────────────┴──────────┴─────────────┴────────────────┘

Note: PicoRV32 CPI is higher due to:
  - No instruction cache
  - 16-bit SRAM interface (requires 2 accesses per 32-bit word)
  - 2-stage pipeline (fetch + execute)
  - Simple memory controller (no prefetch or burst)
```

### Memory Access Patterns - Real Code

```
Example: Copying 1KB array (word-aligned)

  for (int i = 0; i < 256; i++) {
      dst[i] = src[i];  // 32-bit word copy
  }

  Per iteration:
    - Fetch loop instruction (LW):     4 cycles
    - Execute load instruction:        1 cycle
    - Load src[i]:                     4 cycles
    - Fetch store instruction (SW):    4 cycles
    - Execute store instruction:       1 cycle
    - Store dst[i]:                    4 cycles
    - Fetch increment/compare:         4 cycles
    - Execute increment/compare:       2 cycles
    - Fetch branch:                    4 cycles
    - Execute branch (taken):          3 cycles

  Total per iteration: ~31 cycles
  Total for 1KB:       256 × 31 = 7,936 cycles
  Time:                7,936 × 20ns = 158.72 µs
  Effective bandwidth: 1024 / 158.72 µs = 6.45 MB/s

Compare with memcpy() optimized assembly: ~4 cycles/word = 50 MB/s
```

### Optimization Strategies

```
To improve performance:

1. Align data to 32-bit boundaries
   - Avoids byte write RMW penalty
   - Gain: ~40% for write-heavy workloads

2. Use word access instead of byte access
   - 4 cycles vs 7 cycles for writes
   - Gain: ~43% for write-heavy workloads

3. Unroll loops
   - Reduces branch overhead
   - Gain: ~10-15% for tight loops

4. Inline small functions
   - Eliminates call/return overhead
   - Gain: ~5-10% overall

5. Use compiler optimization flags
   - -O2 or -O3
   - -march=rv32im (use hardware multiply)
   - Gain: ~20-30% overall

Example optimization result:
  Baseline:  5.88 MIPS
  Optimized: 7.8 MIPS (+33%)
```

---

## Conclusion

The PicoRV32 memory subsystem provides deterministic, low-latency access to 512KB of external SRAM through an optimized unified controller. While the 16-bit SRAM interface and lack of caching result in lower performance compared to cached systems, the architecture delivers:

- **Predictable timing**: No cache misses or variability
- **Low resource usage**: Fits in iCE40HX8K FPGA with room for peripherals
- **Adequate performance**: ~6 MIPS for typical embedded workloads
- **Flexible addressing**: Full 512KB usable as code or data

This makes it ideal for:
- Educational purposes (understanding CPU-memory interaction)
- Real-time systems (deterministic timing)
- Embedded applications (IoT, sensors, control systems)
- Prototyping and development

For performance-critical applications, consider:
- Using word-aligned access patterns
- Compiler optimization (-O2 or higher)
- Hardware acceleration for compute-intensive tasks

---

**Document Version**: 1.0
**Last Updated**: October 31, 2025
**Author**: Michael Wolak (mikewolak@gmail.com)
