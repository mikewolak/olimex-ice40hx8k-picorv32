# Kconfig Build System Workflow

## Overview
This project uses Kconfig for configuration management. All build settings are centralized in `.config`, which is generated from `configs/defconfig` or via `make menuconfig`.

---

## Complete Build Workflow

### 1. Configure the System

#### Option A: Use Default Configuration (Recommended)
```bash
make defconfig
```

This loads `configs/defconfig` which now includes:
- `CONFIG_COMPRESSED_ISA=y` ✅ **RV32IMC enabled**
- `CONFIG_ENABLE_MUL=y`
- `CONFIG_ENABLE_DIV=y`
- `CONFIG_BARREL_SHIFTER=y`
- All peripheral and memory settings

#### Option B: Interactive Configuration
```bash
make menuconfig
```

Navigate to "PicoRV32 Core Configuration" and enable:
- [x] Enable compressed instructions (RV32IC)
- [x] Enable hardware multiplier
- [x] Enable hardware divider
- [x] Enable barrel shifter

---

### 2. Generate Platform Files

```bash
make generate
```

This runs `scripts/generate_all.sh` which calls:

1. **gen_start.S** → `build/generated/start.S`
   - Startup assembly code
   - Stack pointer initialization
   - BSS clearing

2. **gen_linker.sh** → `build/generated/linker.ld`
   - Linker script with memory layout
   - SRAM sections (code, data, bss, heap, stack)

3. **gen_platform_h.sh** → `build/generated/platform.h`
   - C header with memory addresses
   - Peripheral base addresses
   - Hardware configuration

4. **gen_config_vh.sh** → `build/generated/config.vh`
   - Verilog header with defines
   - **Includes `COMPRESSED_ISA` when CONFIG_COMPRESSED_ISA=y** ✅

---

### 3. Build System Derives Architecture from .config

When building, all Makefiles and scripts read `.config`:

#### Makefiles (Static ARCH Setting)
```makefile
# firmware/Makefile, bootloader/Makefile, etc.
ARCH = rv32imc    # Now hardcoded for RV32IMC
ABI = ilp32
```

#### Build Scripts (Dynamic ARCH Derivation)
```bash
# scripts/build_newlib.sh, build_firmware.sh, etc.
source .config

ARCH="rv32i"
if [ "${CONFIG_ENABLE_MUL}" = "y" ] && [ "${CONFIG_ENABLE_DIV}" = "y" ]; then
    ARCH="${ARCH}m"
fi
if [ "${CONFIG_COMPRESSED_ISA}" = "y" ]; then
    ARCH="${ARCH}c"
fi
# Result: ARCH="rv32imc"
```

---

## Configuration Flow Diagram

```
configs/defconfig
      ↓
  make defconfig
      ↓
   .config (CONFIG_COMPRESSED_ISA=y, CONFIG_ENABLE_MUL=y, etc.)
      ↓
  make generate
      ↓
  ┌─────────────────────────────────────┐
  │ build/generated/                     │
  │   ├── start.S                        │
  │   ├── linker.ld                      │
  │   ├── platform.h                     │
  │   └── config.vh (`define COMPRESSED_ISA) │
  └─────────────────────────────────────┘
      ↓
  Build process
      ↓
  ┌─────────────────────────────────────┐
  │ Makefiles use: ARCH=rv32imc         │
  │ Scripts derive: ARCH=rv32imc        │
  │ Compiler gets: -march=rv32imc       │
  └─────────────────────────────────────┘
      ↓
  RV32IMC binaries
```

---

## File Locations

### Configuration
- **Source**: `configs/defconfig` - Default configuration (version controlled)
- **Active**: `.config` - Active configuration (generated, not in git)
- **Menu**: `Kconfig` - Configuration menu definitions

### Generated Files (auto-created by `make generate`)
- `build/generated/start.S` - Startup assembly
- `build/generated/linker.ld` - Linker script
- `build/generated/platform.h` - C header
- `build/generated/config.vh` - Verilog header

### Symbolic Links (created by Makefile)
- `firmware/start.S` → `../build/generated/start.S`
- `firmware/linker.ld` → `../build/generated/linker.ld`
- `bootloader/start.S` → (uses own start.S)

---

## Verification

### Check Current Configuration
```bash
cat .config | grep COMPRESSED_ISA
# Expected: CONFIG_COMPRESSED_ISA=y
```

### Check Generated Verilog Header
```bash
cat build/generated/config.vh | grep COMPRESSED_ISA
# Expected: `define COMPRESSED_ISA
```

### Check Makefiles Use RV32IMC
```bash
grep "^ARCH" firmware/Makefile bootloader/Makefile
# Expected: ARCH = rv32imc (both files)
```

### Verify Complete System
```bash
./scripts/verify_rv32imc_conversion.sh
# Expected: ✅ ALL CHECKS PASSED!
```

---

## Common Workflows

### Starting Fresh
```bash
make defconfig      # Load default config
make generate       # Generate platform files
make firmware       # Build all firmware
```

### Changing Configuration
```bash
make menuconfig     # Modify settings
make generate       # Regenerate files
make clean          # Clean old builds
make firmware       # Rebuild with new settings
```

### Saving Custom Configuration
```bash
# After modifying .config via menuconfig:
make savedefconfig  # Saves to configs/defconfig
git add configs/defconfig
git commit -m "Update default configuration"
```

---

## Important Notes

### ✅ RV32IMC is Now Default
The defconfig has been updated to enable compressed ISA by default:
```
CONFIG_COMPRESSED_ISA=y
```

### ⚠️ Rebuild After Config Changes
If you modify `.config`, you MUST:
1. Run `make generate` to regenerate platform files
2. Run `make clean` in bootloader and firmware
3. Rebuild libraries if ISA changed (newlib, etc.)
4. Rebuild bootloader and firmware

### 🔧 Kconfig Variables Used by Build Scripts
- `CONFIG_COMPRESSED_ISA` - Enables RV32C extension
- `CONFIG_ENABLE_MUL` - Enables RV32M (multiply)
- `CONFIG_ENABLE_DIV` - Enables RV32M (divide)
- `CONFIG_BARREL_SHIFTER` - Fast shifts
- `CONFIG_FREERTOS_CPU_CLOCK_HZ` - FreeRTOS timing
- `CONFIG_UART_BAUDRATE` - UART speed
- Memory addresses (ROM, SRAM, MMIO bases)

---

## Troubleshooting

### "ERROR: .config not found"
```bash
make defconfig      # Create .config from defaults
```

### Generated files missing
```bash
make generate       # Regenerate platform files
```

### Old builds with wrong ISA
```bash
cd bootloader && make clean
cd firmware && make clean
# Then rebuild
```

### Inconsistent configuration
```bash
# Clean everything and start fresh
make mrproper       # Removes .config and generated files
make defconfig
make generate
```

---

## Summary

✅ **Defconfig updated**: `CONFIG_COMPRESSED_ISA=y`  
✅ **Generation scripts**: Create config.vh with `COMPRESSED_ISA`  
✅ **Build scripts**: Derive `ARCH=rv32imc` from .config  
✅ **Makefiles**: Hardcoded `ARCH=rv32imc`  
✅ **Workflow tested**: defconfig → generate → verify ✓  

The Kconfig system is **fully configured for RV32IMC** and ready for use!
