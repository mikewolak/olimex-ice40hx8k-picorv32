# Library Build Configuration Verification

## Summary
✅ **ALL library build scripts properly handle RV32IMC (compressed ISA)**

All library build systems use CONFIG_COMPRESSED_ISA from .config to dynamically
set the correct architecture flags.

---

## Library Build Scripts

### 1. Newlib C Library
**Script**: `scripts/build_newlib.sh`

**Configuration** (Lines 14-20):
```bash
ARCH="rv32i"
if [ "${CONFIG_ENABLE_MUL}" = "y" ] && [ "${CONFIG_ENABLE_DIV}" = "y" ]; then
    ARCH="${ARCH}m"
fi
if [ "${CONFIG_COMPRESSED_ISA}" = "y" ]; then
    ARCH="${ARCH}c"
fi
```

**Usage** (Line 55):
```bash
CFLAGS_FOR_TARGET="-march=$ARCH -mabi=$ABI -O2 -g"
```

✅ **Status**: Correctly uses CONFIG_COMPRESSED_ISA
- Builds: libc.a, libm.a, libg.a
- Install location: build/sysroot/

---

### 2. Newlib PIC (Position-Independent Code)
**Script**: `scripts/build_newlib_pic.sh`

**Configuration** (Lines 14-20):
```bash
ARCH="rv32i"
if [ "${CONFIG_ENABLE_MUL}" = "y" ] && [ "${CONFIG_ENABLE_DIV}" = "y" ]; then
    ARCH="${ARCH}m"
fi
if [ "${CONFIG_COMPRESSED_ISA}" = "y" ]; then
    ARCH="${ARCH}c"
fi
```

**Usage** (Line 60):
```bash
CFLAGS_FOR_TARGET="-march=$ARCH -mabi=$ABI -O2 -g -fPIC -fno-plt"
```

✅ **Status**: Correctly uses CONFIG_COMPRESSED_ISA with PIC flags
- Builds: PIC versions of libc.a, libm.a, libg.a
- Install location: firmware/overlay_sdk/sysroot_pic/
- Used by: Overlay SDK projects

---

### 3. Overlay Libraries (incurses, microrl)
**Script**: `scripts/build_overlay_libs.sh`

**Configuration** (Lines 14-22):
```bash
ARCH="rv32i"
if [ "${CONFIG_ENABLE_MUL}" = "y" ] && [ "${CONFIG_ENABLE_DIV}" = "y" ]; then
    ARCH="${ARCH}m"
fi
if [ "${CONFIG_COMPRESSED_ISA}" = "y" ]; then
    ARCH="${ARCH}c"
fi
```

**Usage** (Line 29):
```bash
CFLAGS="-march=$ARCH -mabi=$ABI -O2 -g -fPIC -fno-plt -fno-common -ffreestanding -fno-builtin"
```

✅ **Status**: Correctly uses CONFIG_COMPRESSED_ISA with PIC flags
- Builds: libincurses.a, libmicrorl.a
- Install location: firmware/overlay_sdk/sysroot_pic/
- Used by: Overlay SDK projects with curses support

---

### 4. Firmware Build Script
**Script**: `scripts/build_firmware.sh`

**Configuration** (Lines 34-42):
```bash
ARCH="rv32i"
if [ "${CONFIG_ENABLE_MUL}" = "y" ] && [ "${CONFIG_ENABLE_DIV}" = "y" ]; then
    ARCH="${ARCH}m"
fi
if [ "${CONFIG_COMPRESSED_ISA}" = "y" ]; then
    ARCH="${ARCH}c"
fi
```

**Usage** (Line 51):
```bash
CFLAGS="-march=$ARCH -mabi=$ABI -O2 -g -Wall -Wextra"
```

✅ **Status**: Correctly uses CONFIG_COMPRESSED_ISA
- Used by: Individual firmware build targets
- Handles: Bare metal and newlib-based builds

---

## Makefiles (Use $(CFLAGS) from parent)

### 5. lwIP TCP/IP Stack
**File**: `firmware/lwIP/port/lwip.mk`

**Configuration** (Lines 72, 77):
```makefile
$(LWIP_DIR)/%.o: $(LWIP_DIR)/%.c
    @$(CC) $(CFLAGS) $(LWIP_CFLAGS) -c $< -o $@

$(LWIP_PORT_DIR)/%.o: $(LWIP_PORT_DIR)/%.c
    @$(CC) $(CFLAGS) $(LWIP_CFLAGS) -c $< -o $@
```

✅ **Status**: Inherits $(CFLAGS) from firmware/Makefile
- $(CFLAGS) includes -march=rv32imc from parent
- No hardcoded architecture flags
- Builds: lwIP stack objects

---

### 6. SD/FatFS Filesystem
**File**: `firmware/sd_fatfs/Makefile`

**Configuration** (Line 52):
```makefile
%.o: %.c
    @$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@
```

✅ **Status**: Inherits $(CFLAGS) from firmware/Makefile
- $(CFLAGS) includes -march=rv32imc from parent
- No hardcoded architecture flags
- Builds: FatFS and SD card driver objects

---

## FreeRTOS RTOS

**Location**: downloads/freertos/
**Built via**: firmware/Makefile when USE_FREERTOS=1

**Configuration** (firmware/Makefile lines 234-245):
```makefile
# FreeRTOS kernel sources
FREERTOS_SRCS = \
    $(FREERTOS_DIR)/tasks.c \
    $(FREERTOS_DIR)/queue.c \
    ...

# Pattern rules use $(CC) $(CFLAGS)
$(FREERTOS_DIR)/%.o: $(FREERTOS_DIR)/%.c ../.config
    $(CC) $(CFLAGS) -c $< -o $@
```

✅ **Status**: Inherits $(CFLAGS) from firmware/Makefile
- $(CFLAGS) includes -march=rv32imc
- Rebuilds when .config changes
- No hardcoded architecture flags

---

## Verification

Run the verification script:
```bash
./scripts/verify_rv32imc_conversion.sh
```

Expected output:
```
✅ ALL CHECKS PASSED!
```

---

## Build Flow

1. **Configure**: Set CONFIG_COMPRESSED_ISA=y in .config
2. **Generate**: Scripts derive ARCH=rv32imc from config
3. **Libraries**: Build with -march=rv32imc
4. **Firmware**: Link with rv32imc libraries
5. **Result**: All binaries use compressed ISA

---

## Key Points

✅ **No hardcoded rv32im flags anywhere**
✅ **All builds derive ARCH from CONFIG_COMPRESSED_ISA**
✅ **Libraries and firmware use consistent flags**
✅ **PIC builds correctly use -fPIC with rv32imc**
✅ **lwIP, FatFS, FreeRTOS inherit from parent Makefiles**

---

## Rebuild Instructions

When CONFIG_COMPRESSED_ISA changes, rebuild in this order:

```bash
# 1. Clean everything
make clean
cd bootloader && make clean && cd ..
cd firmware && make clean && cd ..

# 2. Rebuild libraries (if installed)
./scripts/build_newlib.sh              # ~30 min
./scripts/build_newlib_pic.sh          # ~30 min (for overlays)
./scripts/build_overlay_libs.sh        # ~1 min

# 3. Rebuild firmware
cd bootloader && make
cd firmware && make firmware

# 4. Rebuild FPGA bitstream
make synth
```

**Expected result**: 25-30% code size reduction across all targets
