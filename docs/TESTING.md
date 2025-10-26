# Testing the Build System

## Quick Test on Fresh Machine

```bash
# Clone repository
git clone https://github.com/mikewolak/olimex-ice40hx8k-picorv32.git
cd olimex-ice40hx8k-picorv32

# Test 1: Load default configuration
make defconfig

# Test 2: Check for tools
make toolchain-check

# Test 3: Download/build toolchains
# Option A: Download pre-built (fast, ~5-10 min)
make toolchain-download

# Option B: Build from source (slow but customizable)
make toolchain-riscv   # ~1-2 hours
make toolchain-fpga    # ~30-45 min (iCE40 only)

# Test 4: Fetch PicoRV32 core
make fetch-picorv32

# Test 5: Generate platform files
make generate

# Test 6: Verify generated files
ls -lh build/generated/
head -20 build/generated/start.S
grep MEMORY build/generated/linker.ld
grep "#define" build/generated/platform.h

# Test 7: Run test suite
make test-generators
```

## What Works Now

✅ **Configuration System**
- `make defconfig` - Loads working configuration
- `.config` created with all parameters

✅ **Toolchain Management**
- `make toolchain-check` - Verify tools are installed
- `make toolchain-download` - Download pre-built tools (~5-10 min)
- `make toolchain-riscv` - Build RISC-V GCC from source (~1-2 hours)
- `make toolchain-fpga` - Build Yosys/NextPNR/IceStorm (~30-45 min, iCE40 only)
- `make fetch-picorv32` - Download PicoRV32 core from GitHub

✅ **Generator Scripts**
- `build/generated/start.S` - IRQ handlers, BSS init, stack setup
- `build/generated/linker.ld` - Memory layout (256KB app, dynamic heap)
- `build/generated/platform.h` - MMIO addresses, helper functions
- `build/generated/config.vh` - Verilog parameters

✅ **Source Files**
- HDL: 21 Verilog files (peripherals + top-level)
- Bootloader: bootloader.c
- Firmware: 21 demo applications
- Libraries: syscalls, microrl, incurses, simple_upload
- Tools: fw_upload (UART uploader)

## Toolchain Options

### Option 1: Pre-built (Recommended for Testing)
```bash
make toolchain-download
```
**Pros:** Fast (~5-10 minutes), reliable
**Cons:** Limited to specific OS/arch, may not match exact config

### Option 2: Build from Source (Recommended for Production)
```bash
make toolchain-riscv   # RISC-V GCC
make toolchain-fpga    # Yosys + NextPNR-ice40 + IceStorm
```
**Pros:** Matches your exact config (rv32im, rv32imc, etc.), fully reproducible
**Cons:** Slow (1-2+ hours total)

### Option 3: Use System Tools
If you already have tools installed:
```bash
# Just verify they exist
make toolchain-check

# If found, skip toolchain build
make generate
```

## What's TODO

⚠ **Build System** (not yet implemented)
- Bootloader compilation
- Firmware compilation
- HDL synthesis (Yosys)
- Place & route (NextPNR)
- Bitstream generation
- Ninja build generator

## Expected Behavior

When complete, this should work:
```bash
make defconfig           # Configure
make toolchain-download  # Get tools (or build from source)
make                     # Build everything
make mrproper            # Clean to pristine state
```

## Current Status

**Phase 1: Toolchain & Generators** ✅ COMPLETE
- Kconfig system
- Platform file generators
- Toolchain fetch/build scripts
- PicoRV32 fetch

**Phase 2: Build System** ⚠ IN PROGRESS
- Bootloader build
- Firmware build
- HDL synthesis
- Full automation

## Dependencies Built

The toolchain scripts will build/download:
- **RISC-V GCC**: Compiler for rv32im/rv32imc
- **Yosys**: Verilog synthesis
- **NextPNR-ice40**: Place & route (iCE40 only, smaller than full NextPNR)
- **IceStorm**: iCE40 bitstream tools (icepack, iceprog, etc.)
- **PicoRV32**: RISC-V soft core (fetched from GitHub)

Total download/build size: ~2-3GB
Repository size: <50MB (everything else is fetched)
