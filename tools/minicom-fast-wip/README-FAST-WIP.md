# Minicom with FAST Streaming Protocol (WIP)

**Status:** Work in Progress - Integration Complete, Testing Needed

## What is This?

This is a modified version of minicom with the **FAST streaming protocol built directly into the source code**. No external programs needed - the FAST upload/download runs natively within minicom.

## Features

- **Built-in FAST Protocol:** Directly integrated at the C code level
- **High-Speed Transfers:** Targets 90-104 KB/sec @ 1 Mbaud
- **No External Dependencies:** FAST protocol runs within minicom process
- **Same Protocol:** Uses proven FAST streaming from bootloader_fast.c/fw_upload_fast.c
  - 3 ACKs total (A, B, C)
  - Continuous streaming (no chunking)
  - CRC32 validation
  - Real-time progress bar

## Files Modified

1. **src/fast-xfr.c** (NEW)
   - Complete FAST protocol implementation
   - `fast_upload()` and `fast_download()` functions

2. **src/updown.c** (MODIFIED)
   - Detects "Fast" protocol selection
   - Bypasses fork/exec, calls built-in functions directly
   - Integration at line 300

3. **src/rwconf.c** (MODIFIED)
   - Protocol entry #10: "YUNYNFast"
   - No external program (pprog10 = "")

4. **src/minicom.h** (MODIFIED)
   - Added fast_upload/fast_download prototypes

5. **src/Makefile.am** (MODIFIED)
   - Added fast-xfr.c to build

## Build Instructions

### Linux

```bash
cd tools/minicom-fast-wip

# Use included local autopoint (no system install needed)
export PATH="$(pwd)/.local-tools/usr/bin:$PATH"
export GETTEXTDATADIR="$(pwd)/.local-tools/usr/share/gettext"

# Generate build system
autoreconf -fi

# Configure (change prefix if desired)
./configure --prefix=$PWD/build

# Build
make -j$(nproc)

# Install locally
make install

# Binary is at: build/bin/minicom
```

### macOS

```bash
cd tools/minicom-fast-wip

# Install dependencies via Homebrew
brew install autoconf automake libtool gettext

# Link gettext (it's keg-only)
export PATH="/opt/homebrew/opt/gettext/bin:$PATH"

# Generate build system
autoreconf -fi

# Configure
./configure --prefix=$PWD/build

# Build
make -j$(sysctl -n hw.ncpu)

# Install locally
make install

# Binary is at: build/bin/minicom
```

## Usage

1. Run minicom:
   ```bash
   ./build/bin/minicom -D /dev/ttyUSB0 -b 1000000
   ```

2. Press `Ctrl-A` then `S` (Send files) or `R` (Receive files)

3. Select **"Fast"** from the protocol menu

4. Select your firmware file

5. FAST streaming protocol runs directly within minicom!

## Protocol Selection in Minicom

When you see the upload/download menu, you'll see:

```
     ┌───────────────────────────────────┐
     │  Zmodem                           │
     │  Ymodem                           │
     │  Xmodem                           │
     │  Kermit                           │
     │  Ascii                            │
     │  Fast          <-- NEW!           │
     └───────────────────────────────────┘
```

## Compatible Firmware

This FAST protocol is compatible with:

- **bootloader_fast.c/bootloader_fast.elf** - FAST streaming bootloader
- **firmware/hexedit_fast.c/hexedit_fast.elf** - FAST hex editor

**NOT compatible with:**
- Standard bootloader.c (uses chunked protocol)
- Standard hexedit.c (uses chunked protocol)

## Testing Status

- ✅ Compiles cleanly on Linux
- ⚠️  Not yet tested on macOS
- ⚠️  Not yet tested with actual FPGA hardware
- ⚠️  Fast download function is stub (only upload implemented)

## Known Issues

1. **Minor build warnings** (non-critical):
   - Unused parameters in `fast_download()` stub
   - Ignored write() return value in `send_uint32_le()`

2. **Download not implemented** - only upload is functional

3. **macOS compatibility** - needs testing with actual hardware

## Architecture Details

### How FAST Integration Works

1. User selects "Fast" protocol from minicom menu
2. `updown()` function (updown.c:300) detects `P_PNAME(g) == "Fast"`
3. Instead of forking and calling external program:
   - Calls `fast_upload(portfd, filename)` directly
   - Uses minicom's already-open serial port (`portfd`)
4. FAST protocol runs in same process
5. Progress updates shown in minicom window
6. Returns to terminal when complete

### FAST Protocol Steps

1. Wait for 'A' (Ready)
2. Send size (4 bytes, little-endian)
3. Wait for 'B' (Size acknowledged)
4. Send CRC32 (4 bytes, little-endian)
5. **Stream ALL data** in 1KB blocks (for progress bar)
6. Wait for 'C' (CRC acknowledged)
7. Receive FPGA CRC32 (4 bytes)
8. Verify CRCs match

## Local Autopoint

The `.local-tools/` directory contains a locally-extracted autopoint binary and data files from the Ubuntu `autopoint_0.21-4ubuntu4_all.deb` package. This allows building without requiring system-wide installation of gettext tools.

**Note:** The autopoint script has been patched to use the local datadir:
```bash
prefix="/tmp/minicom/.local-tools/usr"  # Modified from "/usr"
```

When building on a new machine, you may need to update this path in:
`.local-tools/usr/bin/autopoint` (line 30)

## Next Steps

1. Test build on macOS
2. Test with actual FPGA hardware
3. Fix build warnings
4. Implement fast_download() function
5. Submit upstream patch to minicom project?

## License

Same as minicom - GNU General Public License v2.0 or later

## Author

FAST Protocol Integration: Michael Wolak (October 2025)
- Email: mikewolak@gmail.com, mike@epromfoundry.com
- Based on minicom by Miquel van Smoorenburg

---

**⚠️  WIP STATUS:**
This is experimental code. Use at your own risk. Always have a backup method to program your FPGA.
