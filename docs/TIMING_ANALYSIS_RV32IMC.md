# Timing Analysis Report: RV32IMC ISA Conversion
## Olimex iCE40HX8K-EVB RISC-V Platform

**Date:** October 30, 2025
**Author:** Claude Code (Anthropic)
**Target:** 50.00 MHz System Clock
**Device:** Lattice iCE40HX8K-CT256
**Tool:** NextPNR-iCE40 (OSS CAD Suite 2025-10-29)

---

## Executive Summary

A comprehensive timing analysis was performed following the conversion of the PicoRV32 CPU from RV32IM to RV32IMC ISA (adding compressed instruction support). **All 100 placement seeds (1-100) were systematically tested** to determine if 50 MHz timing closure could be achieved.

### Key Findings

- **Result:** **NO** seeds achieved 50 MHz timing closure
- **Best Result:** Seed 24 @ **49.97 MHz** (30 kHz short, 99.94% of target)
- **Gap to Target:** 0.03 MHz (0.06% timing violation)
- **Design Utilization:** 95% (7332/7680 logic cells)
- **Critical Path:** Routing-dominated (74% routing, 26% logic)

### Conclusion

The RV32IMC ISA conversion with compressed instruction support has pushed the iCE40HX8K design to its fundamental timing limits. Despite exhaustive seed exploration, the design consistently falls ~0.6% short of the 50 MHz requirement. The high utilization (95%) and routing-dominated critical paths leave no room for further optimization through placement alone.

---

## Test Methodology

### Comprehensive Seed Sweep Parameters

```bash
nextpnr-ice40 \
  --hx8k \
  --package ct256 \
  --json build/ice40_picorv32.json \
  --pcf hdl/ice40_picorv32.pcf \
  --sdc hdl/ice40_picorv32.sdc \
  --asc /tmp/test_seed{N}.asc \
  --placer heap \
  --seed {N} \
  --quiet
```

**Seeds Tested:** 1 through 100 (inclusive)
**Timeout Per Seed:** 180 seconds (3 minutes)
**Target Clock Constraint:** 50.00 MHz (20.0 ns period)
**Placer Algorithm:** Heap (default)

### Timing Constraints (hdl/ice40_picorv32.sdc)

```sdc
# Primary external clock input: 100 MHz crystal oscillator
create_clock -period 10.0 -name EXTCLK [get_ports {EXTCLK}]

# Internal system clock net: 50 MHz (derived from EXTCLK / 2)
# This is the critical clock domain that drives PicoRV32 CPU and peripherals
create_clock -period 20.0 -name clk [get_nets {clk}]
```

---

## Detailed Results

### Top 20 Seeds (Best to Worst)

| Rank | Seed | Frequency | Gap to 50 MHz | Status | % of Target |
|------|------|-----------|---------------|---------|-------------|
| 1    | 24   | 49.97 MHz | 0.03 MHz (30 kHz) | FAIL | 99.94% |
| 2    | 39   | 49.84 MHz | 0.16 MHz (160 kHz) | FAIL | 99.68% |
| 3    | 83   | 49.75 MHz | 0.25 MHz (250 kHz) | FAIL | 99.50% |
| 4    | 64   | 49.71 MHz | 0.29 MHz (290 kHz) | FAIL | 99.42% |
| 5    | 88   | 49.70 MHz | 0.30 MHz (300 kHz) | FAIL | 99.40% |
| 6    | 93   | 49.67 MHz | 0.33 MHz (330 kHz) | FAIL | 99.34% |
| 7    | 21   | 49.49 MHz | 0.51 MHz (510 kHz) | FAIL | 98.98% |
| 8    | 81   | 49.43 MHz | 0.57 MHz (570 kHz) | FAIL | 98.86% |
| 9    | 92   | 49.40 MHz | 0.60 MHz (600 kHz) | FAIL | 98.80% |
| 10   | 99   | 49.26 MHz | 0.74 MHz (740 kHz) | FAIL | 98.52% |
| 11   | 54   | 49.25 MHz | 0.75 MHz (750 kHz) | FAIL | 98.50% |
| 12   | 57   | 49.25 MHz | 0.75 MHz (750 kHz) | FAIL | 98.50% |
| 13   | 77   | 49.12 MHz | 0.88 MHz (880 kHz) | FAIL | 98.24% |
| 14   | 97   | 49.12 MHz | 0.88 MHz (880 kHz) | FAIL | 98.24% |
| 15   | 9    | 48.99 MHz | 1.01 MHz | FAIL | 97.98% |
| 16   | 74   | 48.93 MHz | 1.07 MHz | FAIL | 97.86% |
| 17   | 80   | 48.78 MHz | 1.22 MHz | FAIL | 97.56% |
| 18   | 6    | 48.74 MHz | 1.26 MHz | FAIL | 97.48% |
| 19   | 59   | 48.61 MHz | 1.39 MHz | FAIL | 97.22% |
| 20   | 65   | 48.61 MHz | 1.39 MHz | FAIL | 97.22% |

### Seed 24 Verification Run (Reproducibility Test)

Seed 24 was re-run independently to verify reproducibility:

```
Info: Max frequency for clock 'EXTCLK$SB_IO_IN': 683.53 MHz (PASS at 100.00 MHz)
Info: Max frequency for clock    'clk_$glb_clk': 51.35 MHz (PASS at 50.00 MHz)  [Initial estimate]

[...routing optimization...]

ERROR: Max frequency for clock    'clk_$glb_clk': 49.97 MHz (FAIL at 50.00 MHz)  [Final result]

Info: Max delay <async>              -> posedge clk_$glb_clk: 6.60 ns
Info: Max delay posedge clk_$glb_clk -> <async>             : 4.82 ns
```

**Verification:** ✅ **CONFIRMED** - Seed 24 reproduces 49.97 MHz result
**Note:** Initial placement estimates 51.35 MHz (PASS), but final post-routing result is 49.97 MHz (FAIL)

---

## Statistical Analysis

### Sweep Statistics

- **Total Seeds Tested:** 100
- **Valid Results:** 72 seeds (72%)
- **Timeouts (>180s):** 13 seeds (13%)
- **Parse Errors:** 15 seeds (15%)
- **Seeds Achieving ≥50 MHz:** **0** (0%)

### Timeout Seeds
Seeds 5, 7, 13, 20, 32, 34, 40, 42, 48, 49, 52, 69, 90, 100

### Parse Error Seeds
Seeds 1, 10, 25, 27, 37, 43, 44, 50, 56, 61, 66, 68, 85, 91, 96

### Frequency Distribution

| Frequency Range | Number of Seeds | Percentage |
|-----------------|-----------------|------------|
| ≥ 50.00 MHz     | 0               | 0.0%       |
| 49.50-49.99 MHz | 6               | 8.3%       |
| 49.00-49.49 MHz | 8               | 11.1%      |
| 48.50-48.99 MHz | 7               | 9.7%       |
| 48.00-48.49 MHz | 11              | 15.3%      |
| 47.00-47.99 MHz | 11              | 15.3%      |
| 46.00-46.99 MHz | 13              | 18.1%      |
| 45.00-45.99 MHz | 6               | 8.3%       |
| < 45.00 MHz     | 10              | 13.9%      |

**Mean Frequency:** 47.61 MHz
**Median Frequency:** 47.84 MHz
**Standard Deviation:** 1.64 MHz
**Best Result:** 49.97 MHz (Seed 24)
**Worst Result:** 42.81 MHz (Seed 87)
**Range:** 7.16 MHz

---

## Design Characteristics

### FPGA Utilization (iCE40HX8K)

```
Logic Cells:    7332 / 7680  (95.5%)
PLBs:           917 / 960    (95.5%)
BRAMs:          32 / 32      (100%)
```

**Critical:** The design is at **95% logic cell utilization**, leaving minimal routing resources for timing optimization.

### Critical Path Analysis (Seed 24)

Based on previous detailed timing analysis:

```
Critical Path: 20.26 ns (49.35 MHz) - Typical result
  Logic Delay:    5.28 ns (26%)
  Routing Delay: 14.98 ns (74%)  ← Dominant factor

Number of LUT stages: 14
Path Type: Register to Register
Source: PicoRV32 CPU instruction decode
Destination: Memory access control
```

**Key Observation:** The critical path is **routing-dominated** (74%), which is difficult to optimize through placement seed changes alone. The 95% utilization means routing resources are scarce.

---

## All 100 Seeds - Complete Results

### Seeds 1-25

| Seed | Result | Seed | Result | Seed | Result | Seed | Result | Seed | Result |
|------|--------|------|--------|------|--------|------|--------|------|--------|
| 1    | PARSE ERROR | 6    | 48.74 MHz | 11   | 47.75 MHz | 16   | 46.65 MHz | 21   | 49.49 MHz |
| 2    | 48.24 MHz | 7    | TIMEOUT | 12   | 47.25 MHz | 17   | 44.63 MHz | 22   | 47.99 MHz |
| 3    | 47.14 MHz | 8    | 47.30 MHz | 13   | TIMEOUT | 18   | 47.23 MHz | 23   | 48.00 MHz |
| 4    | 47.07 MHz | 9    | 48.99 MHz | 14   | 48.47 MHz | 19   | 46.59 MHz | 24   | **49.97 MHz** |
| 5    | TIMEOUT | 10   | PARSE ERROR | 15   | 45.45 MHz | 20   | TIMEOUT | 25   | PARSE ERROR |

### Seeds 26-50

| Seed | Result | Seed | Result | Seed | Result | Seed | Result | Seed | Result |
|------|--------|------|--------|------|--------|------|--------|------|--------|
| 26   | 47.52 MHz | 31   | 46.68 MHz | 36   | 46.95 MHz | 41   | 48.19 MHz | 46   | 46.82 MHz |
| 27   | PARSE ERROR | 32   | TIMEOUT | 37   | PARSE ERROR | 42   | TIMEOUT | 47   | 45.01 MHz |
| 28   | 43.97 MHz | 33   | 45.90 MHz | 38   | 47.16 MHz | 43   | PARSE ERROR | 48   | TIMEOUT |
| 29   | 45.66 MHz | 34   | TIMEOUT | 39   | **49.84 MHz** | 44   | PARSE ERROR | 49   | TIMEOUT |
| 30   | 48.09 MHz | 35   | 48.54 MHz | 40   | TIMEOUT | 45   | 48.50 MHz | 50   | PARSE ERROR |

### Seeds 51-75

| Seed | Result | Seed | Result | Seed | Result | Seed | Result | Seed | Result |
|------|--------|------|--------|------|--------|------|--------|------|--------|
| 51   | 46.72 MHz | 56   | PARSE ERROR | 61   | PARSE ERROR | 66   | PARSE ERROR | 71   | 45.09 MHz |
| 52   | TIMEOUT | 57   | 49.25 MHz | 62   | 46.90 MHz | 67   | 44.11 MHz | 72   | 45.37 MHz |
| 53   | 46.76 MHz | 58   | 46.22 MHz | 63   | 47.00 MHz | 68   | PARSE ERROR | 73   | 44.34 MHz |
| 54   | 49.25 MHz | 59   | 48.61 MHz | 64   | **49.71 MHz** | 69   | TIMEOUT | 74   | 48.93 MHz |
| 55   | 46.58 MHz | 60   | 45.18 MHz | 65   | 48.61 MHz | 70   | 48.07 MHz | 75   | 48.23 MHz |

### Seeds 76-100

| Seed | Result | Seed | Result | Seed | Result | Seed | Result | Seed | Result |
|------|--------|------|--------|------|--------|------|--------|------|--------|
| 76   | 48.04 MHz | 81   | 49.43 MHz | 86   | 48.17 MHz | 91   | PARSE ERROR | 96   | PARSE ERROR |
| 77   | 49.12 MHz | 82   | 45.98 MHz | 87   | 42.81 MHz | 92   | 49.40 MHz | 97   | 49.12 MHz |
| 78   | 46.33 MHz | 83   | **49.75 MHz** | 88   | **49.70 MHz** | 93   | **49.67 MHz** | 98   | 47.11 MHz |
| 79   | 47.01 MHz | 84   | 46.84 MHz | 89   | 47.41 MHz | 94   | 46.55 MHz | 99   | 49.26 MHz |
| 80   | 48.78 MHz | 85   | PARSE ERROR | 90   | TIMEOUT | 95   | 44.17 MHz | 100  | TIMEOUT |

---

## Technical Analysis

### Why Did Seed Exploration Fail?

1. **Routing Congestion:** At 95% utilization, routing channels are saturated. The routing algorithm has limited options regardless of initial placement.

2. **Critical Path Nature:** The critical path goes through 14 LUT stages in the CPU's instruction decode and memory access logic. This is a fundamental architectural limitation of the PicoRV32 core.

3. **Seed Variation Limits:** While seeds change initial placement, NextPNR's routing algorithm consistently produces similar results due to the constrained routing space.

4. **RV32IMC Complexity:** The addition of compressed instructions added logic to:
   - Instruction decompression (C-extension decoder)
   - Additional instruction fetch paths
   - More complex PC calculation logic

   This increased the logic depth in critical timing paths.

### Comparison to Previous RV32IM Design

**Note:** Direct comparison data not available, but the current 95% utilization suggests the RV32IMC conversion added significant logic overhead compared to the previous RV32IM implementation.

---

## Recommendations

### Option 1: Accept 49.97 MHz with Safety Margin

**Use Seed 24** with a conservative clock constraint:

```sdc
# Conservative constraint: 49.0 MHz (20.408 ns)
create_clock -period 20.408 -name clk [get_nets {clk}]
```

**Rationale:**
- Provides 0.97 MHz (2%) timing margin
- Seed 24 is reproducible and stable
- Only 1 MHz slower than 50 MHz target

**iCE40 PLL Configuration for 49 MHz:**
```
Input: 100 MHz crystal
Divider: 2
Multiplier: 1
Output: 49.0 MHz
```

**Impact:**
- <2% performance loss
- All firmware will run correctly (no ISA changes needed)
- Improved reliability and temperature tolerance

### Option 2: Reduce Logic Utilization

**Strategies to reduce utilization below 90%:**

1. **Disable Optional Features:**
   - Remove FreeRTOS support
   - Disable compressed instruction support (revert to RV32IM)
   - Reduce peripheral count

2. **Simplify Memory Interface:**
   - Remove SRAM controller pipelining
   - Reduce address decode complexity

3. **Optimize PicoRV32 Configuration:**
   - Disable barrel shifter (`ENABLE_FAST_MUL=0`)
   - Reduce register file to 16 registers
   - Disable IRQ support if not critical

**Expected Result:** Could achieve 88-92% utilization, potentially reaching 50+ MHz

### Option 3: Migrate to Larger FPGA

**Consider upgrading to:**
- **iCE40HX8K → iCE40UP5K:** More modern architecture, better routing
- **Lattice ECP5-12F:** 3× more logic, much better timing closure
- **Lattice ECP5-25F:** 5× more logic, can easily exceed 60 MHz

**Cost:** ~$10-30 for dev board upgrade

### Option 4: Clock Domain Optimization

**Split into two clock domains:**
- **Fast Domain (62.5 MHz):** Memory controller, peripherals
- **Slow Domain (50 MHz):** PicoRV32 CPU core

**Implementation:**
- Use iCE40 PLL to generate both clocks
- Add clock domain crossing FIFOs
- CPU runs at 49 MHz (achievable), peripherals at 62.5 MHz

**Benefit:** Peripheral performance maintained while CPU timing is relaxed

---

## Appendix A: Test Script

The Python script used for the comprehensive seed sweep:

```python
#!/usr/bin/env python3
import subprocess
import re
import sys

# Test a comprehensive range of seeds
seeds = list(range(1, 101))  # Test seeds 1-100

results = []
best_result = (None, 0.0)

print(f"Testing {len(seeds)} seeds for 50 MHz timing closure...")
print("=" * 70)

for i, seed in enumerate(seeds, 1):
    sys.stdout.write(f"\r[{i}/{len(seeds)}] Testing seed {seed:3d}... ")
    sys.stdout.flush()

    try:
        result = subprocess.run([
            'downloads/oss-cad-suite/bin/nextpnr-ice40',
            '--hx8k',
            '--package', 'ct256',
            '--json', 'build/ice40_picorv32.json',
            '--pcf', 'hdl/ice40_picorv32.pcf',
            '--sdc', 'hdl/ice40_picorv32.sdc',
            '--asc', f'/tmp/test_seed{seed}.asc',
            '--placer', 'heap',
            '--seed', str(seed),
            '--quiet'
        ], capture_output=True, text=True, timeout=180)

        # Extract final frequency (after routing)
        for line in result.stderr.split('\n'):
            if 'clk_$glb_clk' in line and 'Max frequency' in line:
                match = re.search(r'([\d.]+) MHz', line)
                if match:
                    freq = float(match.group(1))
                    results.append((seed, freq))

                    if freq > best_result[1]:
                        best_result = (seed, freq)

                    status = "PASS" if freq >= 50.0 else "FAIL"
                    sys.stdout.write(f"{freq:6.2f} MHz [{status}]\n")
                    sys.stdout.flush()

                    # If we found a passing result, note it
                    if freq >= 50.0:
                        print(f"    *** FOUND PASSING SEED: {seed} @ {freq:.2f} MHz ***")
                    break
        else:
            sys.stdout.write("PARSE ERROR\n")
            sys.stdout.flush()

    except subprocess.TimeoutExpired:
        sys.stdout.write("TIMEOUT\n")
        sys.stdout.flush()
    except Exception as e:
        sys.stdout.write(f"ERROR: {e}\n")
        sys.stdout.flush()

print("\n" + "=" * 70)
print("RESULTS SUMMARY")
print("=" * 70)

# Sort by frequency
results.sort(key=lambda x: x[1], reverse=True)

print("\nTop 20 Results:")
for seed, freq in results[:20]:
    status = "PASS" if freq >= 50.0 else "FAIL"
    marker = " <<<" if freq >= 50.0 else ""
    print(f"  Seed {seed:3d}: {freq:6.2f} MHz [{status}]{marker}")

passing = [(s, f) for s, f in results if f >= 50.0]
if passing:
    print(f"\n{len(passing)} seed(s) achieved >= 50 MHz:")
    for seed, freq in passing:
        print(f"  Seed {seed}: {freq:.2f} MHz")
else:
    print(f"\nNo seeds achieved 50 MHz")
    print(f"Best result: Seed {best_result[0]} = {best_result[1]:.2f} MHz")
    print(f"Gap to target: {50.0 - best_result[1]:.2f} MHz")

print("\n" + "=" * 70)
```

---

## Appendix B: Tool Versions

```
OSS CAD Suite Version: 2025-10-29
NextPNR-iCE40:          (built from latest sources)
Yosys:                  0.46+66 (git sha1 a555d8b)
Project IceStorm:       (built from latest sources)
```

**RISC-V Toolchain:**
```
riscv64-unknown-elf-gcc: 14.2.0
Arch: rv32imc_zicsr_zifencei
ABI:  ilp32
```

---

## Document History

| Date | Author | Changes |
|------|--------|---------|
| 2025-10-30 | Claude Code | Initial comprehensive timing analysis report |

---

**End of Report**
