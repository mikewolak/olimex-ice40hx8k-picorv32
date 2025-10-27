# SRAM Driver and Memory Controller Optimization Analysis

**Date**: October 27, 2025
**Author**: Michael Wolak
**Target**: Olimex iCE40HX8K-EVB with IS61WV51216BLL-10TLI SRAM (10ns access time)

---

## Current Architecture

### Three-Layer Design

1. **mem_controller.v** - Memory routing (SRAM/Boot ROM/MMIO)
2. **sram_proc_new.v** - 32-bit to 16-bit transaction converter
3. **sram_driver_new.v** - Physical SRAM interface driver

### Current Latency Analysis

#### SRAM Driver (sram_driver_new.v)
**States per 16-bit transaction**: 5 states, 4 cycles
- IDLE (0 cycles - waiting)
- SETUP (1 cycle - address setup, assert CS)
- ACTIVE (1 cycle - assert WE for write, sample data for read)
- RECOVERY (1 cycle - deassert WE, complete transaction)
- COOLDOWN (1 cycle - mandatory gap before next transaction)

**Total**: 4 cycles @ 50 MHz = 80ns per 16-bit access

#### SRAM Processor (sram_proc_new.v)
**32-bit Read Sequence**:
- STATE_READ_LOW (1 cycle + 4 cycles in driver)
- STATE_READ_WAIT1 (1 cycle wait)
- STATE_READ_SETUP_HIGH (1 cycle setup)
- STATE_READ_HIGH (1 cycle + 4 cycles in driver)
- STATE_READ_WAIT2 (1 cycle wait)
- STATE_COMPLETE (1 cycle)
- STATE_DONE (1 cycle)

**Total**: ~13-14 cycles @ 50 MHz = 260-280ns per 32-bit read

**32-bit Write Sequence** (full word):
- STATE_WRITE_LOW (1 cycle + 4 cycles in driver)
- STATE_WRITE_WAIT1 (1 cycle wait)
- STATE_WRITE_SETUP_HIGH (1 cycle setup)
- STATE_WRITE_HIGH (1 cycle + 4 cycles in driver)
- STATE_WRITE_WAIT2 (1 cycle wait)
- STATE_DONE (1 cycle)

**Total**: ~13-14 cycles @ 50 MHz = 260-280ns per 32-bit write

**32-bit Write Sequence** (byte/halfword - with RMW):
- Read both 16-bit words (13-14 cycles)
- Merge with strobes (1 cycle)
- Write both 16-bit words (13-14 cycles)

**Total**: ~27-28 cycles @ 50 MHz = 540-560ns per partial write

#### Memory Controller (mem_controller.v)
**Overhead**: 1-2 cycles for routing and state management

### Total System Latency
- **32-bit Read**: ~15 cycles = 300ns
- **32-bit Write (full)**: ~15 cycles = 300ns
- **32-bit Write (partial)**: ~29 cycles = 580ns

---

## SRAM Chip Specifications

**Part**: IS61WV51216BLL-10TLI (512KB, 10ns access time)
**Organization**: 512K × 16-bit
**Technology**: CMOS Static RAM

### Critical Timing Parameters @ 10ns Speed Grade

| Parameter | Min | Max | Unit | Description |
|-----------|-----|-----|------|-------------|
| tAA | - | 10 | ns | Address Access Time |
| tOHA | 3 | - | ns | Output Hold Time |
| tACE | - | 10 | ns | Chip Enable Access Time |
| tDOE | - | 5 | ns | Output Enable Access Time |
| tPU | - | 0 | ns | Chip Enable to Power Up |
| tPD | - | 5 | ns | Chip Enable to Power Down |
| tAS | 0 | - | ns | Address Setup Time |
| tAH | 0 | - | ns | Address Hold Time |
| tWP | 7 | - | ns | Write Pulse Width |
| tWR | 0 | - | ns | Write Recovery Time |
| tDW | 5 | - | ns | Data Setup to Write End |
| tDH | 0 | - | ns | Data Hold from Write End |
| tOW | 3 | - | ns | Write Enable to Output Active |

### Critical Observations

1. **Address Access Time (tAA)**: 10ns max - We have 20ns @ 50 MHz (1 cycle)
2. **Write Pulse Width (tWP)**: 7ns min - We have 20ns @ 50 MHz (1 cycle) ✅
3. **Data Setup Time (tDW)**: 5ns min - We have 20ns @ 50 MHz (1 cycle) ✅
4. **Write Recovery (tWR)**: 0ns min - No recovery time required! ✅
5. **Address Setup (tAS)**: 0ns min - Can change immediately ✅

**KEY INSIGHT**: SRAM has 0ns write recovery time and 0ns address setup time!
This means we can do back-to-back transactions without COOLDOWN or wait states.

---

## Optimization Opportunities

### 1. Eliminate COOLDOWN State
**Current**: Mandatory 1-cycle gap after every transaction
**SRAM Spec**: tWR = 0ns (no recovery needed)
**Savings**: 1 cycle per 16-bit access = 2 cycles per 32-bit access

### 2. Eliminate Inter-Transaction Wait States
**Current**: WAIT1 and WAIT2 states in sram_proc_new
**Reason**: Probably added for safety/debugging
**Reality**: Not required by SRAM timing
**Savings**: 2 cycles per 32-bit access

### 3. Merge sram_proc_new and sram_driver_new
**Current**: Two separate state machines with handshaking overhead
**Proposed**: Single unified state machine
**Benefits**:
- Eliminate valid/ready handshaking latency
- Simplify control logic
- Direct control of SRAM pins based on 32-bit transaction state
- Easier timing optimization

### 4. Optimize Read Path
**Current**: 3 cycles per 16-bit read (SETUP + ACTIVE + RECOVERY)
**Possible**: 2 cycles per 16-bit read (SETUP + CAPTURE)
- Cycle 1: Assert address + CS + OE
- Cycle 2: Sample data (20ns after address = meets tAA of 10ns max)

**Savings**: 1 cycle per 16-bit read = 2 cycles per 32-bit read

### 5. Optimize Write Path
**Current**: 3 cycles per 16-bit write (SETUP + ACTIVE + RECOVERY)
**Possible**: 2 cycles per 16-bit write (SETUP + WRITE)
- Cycle 1: Assert address + CS + data
- Cycle 2: Assert WE (20ns pulse = meets tWP of 7ns min)

**Savings**: 1 cycle per 16-bit write = 2 cycles per 32-bit write

---

## Proposed Optimized Architecture

### Single Unified Module: `sram_controller_optimized.v`

Combines mem_controller routing logic with direct SRAM control.

### Optimized 32-bit Read Sequence
```
Cycle 0: IDLE (cpu_mem_valid arrives)
Cycle 1: Setup low word address, assert CS + OE
Cycle 2: Sample low word data, setup high word address
Cycle 3: Sample high word data, assert cpu_mem_ready
Total: 3 cycles = 60ns (5x faster than current!)
```

### Optimized 32-bit Write Sequence
```
Cycle 0: IDLE (cpu_mem_valid arrives with wstrb)
Cycle 1: Setup low word address + data, assert CS + WE
Cycle 2: Setup high word address + data, assert CS + WE
Cycle 3: Deassert WE, assert cpu_mem_ready
Total: 3 cycles = 60ns (5x faster than current!)
```

### Read-Modify-Write for Partial Writes
```
Cycle 0: IDLE
Cycle 1-3: Read current 32-bit value (3 cycles)
Cycle 4: Merge with new data based on wstrb
Cycle 5-7: Write merged 32-bit value (3 cycles)
Total: 7 cycles = 140ns (4x faster than current!)
```

---

## Testing Strategy

### Phase 1: Baseline Testing (CRITICAL - DO THIS FIRST!)

Before making ANY changes, we must establish a comprehensive test suite on the CURRENT implementation.

#### Test 1: Memory Pattern Test
```c
// firmware/memory_test_baseline.c
void test_sequential_writes() {
    volatile uint32_t *mem = (uint32_t *)0x00000000;
    for (int i = 0; i < 1024; i++) {
        mem[i] = 0x12345678 + i;
    }
    for (int i = 0; i < 1024; i++) {
        if (mem[i] != 0x12345678 + i) FAIL();
    }
}

void test_random_access() {
    volatile uint32_t *mem = (uint32_t *)0x00000000;
    // Write random addresses
    mem[0] = 0xDEADBEEF;
    mem[1000] = 0xCAFEBABE;
    mem[5] = 0x12345678;
    // Verify
    if (mem[0] != 0xDEADBEEF) FAIL();
    if (mem[1000] != 0xCAFEBABE) FAIL();
    if (mem[5] != 0x12345678) FAIL();
}

void test_byte_writes() {
    volatile uint8_t *mem8 = (uint8_t *)0x00000000;
    volatile uint32_t *mem32 = (uint32_t *)0x00000000;

    mem32[0] = 0x00000000;  // Clear
    mem8[0] = 0x11;
    mem8[1] = 0x22;
    mem8[2] = 0x33;
    mem8[3] = 0x44;

    if (mem32[0] != 0x44332211) FAIL();
}

void test_halfword_writes() {
    volatile uint16_t *mem16 = (uint16_t *)0x00000000;
    volatile uint32_t *mem32 = (uint32_t *)0x00000000;

    mem32[0] = 0x00000000;  // Clear
    mem16[0] = 0xBEEF;
    mem16[1] = 0xDEAD;

    if (mem32[0] != 0xDEADBEEF) FAIL();
}
```

#### Test 2: Timing Benchmark
```c
// firmware/memory_benchmark_baseline.c
uint32_t benchmark_sequential_read() {
    volatile uint32_t *mem = (uint32_t *)0x00000000;
    uint32_t start = get_cycles();
    uint32_t sum = 0;
    for (int i = 0; i < 1000; i++) {
        sum += mem[i];
    }
    uint32_t end = get_cycles();
    return end - start;
}

uint32_t benchmark_sequential_write() {
    volatile uint32_t *mem = (uint32_t *)0x00000000;
    uint32_t start = get_cycles();
    for (int i = 0; i < 1000; i++) {
        mem[i] = i;
    }
    uint32_t end = get_cycles();
    return end - start;
}
```

#### Test 3: Stress Test
```c
void stress_test_alternating_rw() {
    volatile uint32_t *mem = (uint32_t *)0x00000000;
    for (int iter = 0; iter < 100; iter++) {
        // Write pattern
        for (int i = 0; i < 100; i++) {
            mem[i] = 0xA5A5A5A5 ^ i ^ iter;
        }
        // Read and verify
        for (int i = 0; i < 100; i++) {
            if (mem[i] != (0xA5A5A5A5 ^ i ^ iter)) FAIL();
        }
    }
}
```

#### Test 4: Edge Cases
```c
void test_back_to_back_transactions() {
    volatile uint32_t *mem = (uint32_t *)0x00000000;
    mem[0] = 0x11111111;
    mem[1] = 0x22222222;  // Immediate back-to-back write
    uint32_t v0 = mem[0];
    uint32_t v1 = mem[1];  // Immediate back-to-back read
    if (v0 != 0x11111111 || v1 != 0x22222222) FAIL();
}

void test_word_boundary_crossing() {
    volatile uint32_t *mem = (uint32_t *)0x0000FFFC;  // Near 64KB boundary
    mem[0] = 0xDEADBEEF;
    mem[1] = 0xCAFEBABE;
    if (mem[0] != 0xDEADBEEF) FAIL();
    if (mem[1] != 0xCAFEBABE) FAIL();
}
```

### Phase 2: Create Optimized Module

Only after Phase 1 passes 100%, create `sram_controller_optimized.v`:
- Merge routing logic from mem_controller
- Merge transaction logic from sram_proc_new
- Direct SRAM pin control from sram_driver_new
- 3-cycle read/write sequences

### Phase 3: Regression Testing

Run EXACT SAME tests from Phase 1 on new implementation:
- Must pass 100% of functional tests
- Benchmark timing should show 3-5x speedup
- NO failures allowed

### Phase 4: Extended Burn-In

- Run stress tests for 1 hour
- Test at different temperatures (if possible)
- Test with different UART/SPI activity (concurrent MMIO)
- Test overlay loading and execution

---

## Implementation Plan

### Step 1: Create Baseline Test Suite ✅ MUST DO FIRST
- [ ] Create firmware/memory_test_baseline.c
- [ ] Build and run on current hardware
- [ ] Document all timing results
- [ ] Ensure 100% pass rate

### Step 2: Document Current Behavior
- [ ] Capture logic analyzer traces of read/write sequences
- [ ] Measure actual cycle counts
- [ ] Compare with theoretical analysis above

### Step 3: Design Optimized Module
- [ ] Create sram_controller_optimized.v
- [ ] Single state machine
- [ ] 3-cycle read/write sequences
- [ ] Maintain mem_controller routing logic

### Step 4: Incremental Integration
- [ ] Add Makefile option: USE_OPTIMIZED_SRAM=1
- [ ] Keep both implementations in repo
- [ ] Easy A/B comparison

### Step 5: Validation
- [ ] Run Phase 1 tests on new implementation
- [ ] Compare timing benchmarks
- [ ] Fix any failures before proceeding

### Step 6: Production Deployment
- [ ] Switch default to optimized version
- [ ] Update documentation
- [ ] Keep old version as reference

---

## Risks and Mitigations

### Risk 1: Timing Violations
**Mitigation**: Logic analyzer verification, conservative first implementation

### Risk 2: Back-to-Back Transaction Issues
**Mitigation**: Comprehensive edge case testing

### Risk 3: Integration with Existing Code
**Mitigation**: Keep mem_controller interface unchanged, drop-in replacement

### Risk 4: Hard-to-Debug Failures
**Mitigation**: Extensive debug $display statements in simulation

---

## Expected Performance Improvement

### Current Performance
- Read: 15 cycles = 300ns
- Write (full): 15 cycles = 300ns
- Write (partial): 29 cycles = 580ns

### Optimized Performance (Target)
- Read: 3 cycles = 60ns (**5x faster**)
- Write (full): 3 cycles = 60ns (**5x faster**)
- Write (partial): 7 cycles = 140ns (**4x faster**)

### System-Level Impact
- Faster instruction fetch
- Faster data access
- Higher effective CPU MIPS
- Better overlay performance
- Smoother graphics/ncurses updates

---

## Conclusion

The current SRAM subsystem is **over-engineered** with unnecessary wait states and cooldown cycles. The SRAM chip specifications allow for much more aggressive timing.

**However**, we MUST NOT make changes without comprehensive testing first. The memory subsystem is the foundation of the entire system - any bugs here will cause catastrophic failures that are very difficult to debug.

**Next Action**: Implement Phase 1 baseline test suite and run it on current hardware to establish known-good behavior before proceeding with optimization.
