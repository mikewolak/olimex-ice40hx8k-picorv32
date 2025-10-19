# Minicom-FPGA: PicoRV32 Custom Terminal

**Version:** 2.10.90-PicoRV32
**Status:** Production Ready

## What is This?

Minicom-FPGA is a customized version of minicom 2.10.90 with the **FAST streaming protocol built directly into the source code**. Designed specifically for ultra-fast firmware uploads to iCE40HX8K FPGA running PicoRV32.

No external programs needed - the FAST upload runs natively within minicom at 90-104 KB/sec.

## Features

- **Built-in FAST Protocol:** Directly integrated at the C code level
- **High-Speed Transfers:** 90-104 KB/sec @ 1 Mbaud
- **No External Dependencies:** FAST protocol runs within minicom process
- **Streamlined UX:** Single protocol, menu-free file selection
- **Cross-Platform:** Works on Linux and macOS
- **Proven Protocol:** Same FAST streaming from bootloader_fast.c/fw_upload_fast.c
  - Only 3 ACKs total (vs 4096+ in standard chunked)
  - Continuous streaming (no per-chunk ACKs)
  - CRC32 validation
  - 4-second timeout protection

## Performance

| Method | ACKs | Upload Time (256KB) | Speed |
|--------|------|---------------------|-------|
| Standard Bootloader | 4096+ | ~20 seconds | ~13 KB/sec |
| FAST Protocol (Linux) | 3 | ~2.3 seconds | ~90 KB/sec |
| FAST Protocol (macOS) | 3 | ~2.3 seconds | ~104 KB/sec |

## Files Modified

1. **src/fast-xfr.c** (NEW)
   - Complete FAST protocol implementation
   - `fast_upload()` and `fast_download()` functions
   - Timeout protection (4 seconds)

2. **src/updown.c** (MODIFIED)
   - Detects "Fast" protocol selection
   - Bypasses fork/exec, calls built-in functions directly
   - Menu bypass when only one protocol configured

3. **src/rwconf.c** (MODIFIED)
   - Single protocol entry: "YUNYNFast"
   - No external program dependencies

4. **src/minicom.h** (MODIFIED)
   - Added fast_upload/fast_download prototypes

5. **src/Makefile.am** (MODIFIED)
   - Added fast-xfr.c to build

6. **configure.ac** (MODIFIED)
   - Updated version to "Minicom-FPGA 2.10.90-PicoRV32"

## Build Instructions

### Using build.sh (Recommended)

```bash
cd tools/minicom-picorv32
./build.sh
```

The build script:
- Auto-detects platform (Linux/macOS)
- Uses local autopoint/gettext tools (no system dependencies)
- Configures and compiles
- Binary is at: `src/minicom`

### Manual Build - Linux

```bash
cd tools/minicom-picorv32

# Use included local autopoint (no system install needed)
export PATH="$(pwd)/.local-tools/usr/bin:$PATH"
export GETTEXTDATADIR="$(pwd)/.local-tools/usr/share/gettext"

# Generate build system
autoreconf -fi

# Configure (change prefix if desired)
./configure --prefix=$PWD/build

# Build
make -j$(nproc)

# Binary is at: src/minicom
```

### Manual Build - macOS

```bash
cd tools/minicom-picorv32

# Install dependencies via Homebrew
brew install gettext
export PATH="/opt/homebrew/opt/gettext/bin:$PATH"

# Generate build system
autoreconf -fi

# Configure
./configure --prefix=$PWD/build

# Build
make -j$(sysctl -n hw.ncpu)

# Binary is at: src/minicom
```

## Usage

1. **Connect to FPGA:**
   ```bash
   src/minicom -D /dev/ttyUSB0 -b 1000000
   ```

2. **Upload firmware:**
   - Press `Ctrl-A` to enter command mode
   - Press `S` for send/upload
   - File browser appears - navigate and select your firmware file
   - Upload happens automatically with real-time progress
   - Returns to terminal when complete

**That's it!** No menu navigation, no protocol selection, just works.

## Compatible Firmware

This FAST protocol works with:

- **bootloader/bootloader_fast.c** - FAST streaming bootloader
- **firmware/hexedit_fast.c** - FAST hex editor firmware

**NOT compatible with:**
- Standard bootloader.c (uses chunked protocol)
- Standard hexedit.c (uses chunked protocol)

## Protocol Details

### FAST Protocol Sequence

1. PC sends 'R' (Ready) → FPGA responds 'A' (ACK)
2. PC sends 4-byte size → FPGA responds 'B' (ACK)
3. PC streams ALL data continuously (NO per-chunk ACKs!)
4. PC sends 'C' + 4-byte CRC32
5. FPGA calculates CRC, responds 'C' + calculated CRC32
6. PC verifies CRC match

**Key Innovation:** Step 3 streams the entire firmware in one continuous transfer with zero interruptions. Standard chunked protocols send ~64 bytes, wait for ACK, send next chunk, repeat 4096+ times.

### Integration Architecture

1. User presses Ctrl-A + S to upload
2. File browser appears (menu bypassed - only one protocol)
3. User selects firmware file
4. `updown()` function (updown.c:300) detects `P_PNAME(g) == "Fast"`
5. Instead of forking external program:
   - Calls `fast_upload(portfd, filename)` directly
   - Uses minicom's already-open serial port (`portfd`)
6. FAST protocol runs in same process
7. Returns to terminal when complete

## Timeout Protection

All blocking operations have 4-second timeout protection:
- `wait_for_char()` - waits for 'A', 'B', 'C' responses
- `read_uint32_le()` - reads CRC32 from FPGA
- Data streaming loop - monitors total transmission time

If any operation exceeds 4 seconds, the transfer aborts cleanly with proper resource cleanup. This prevents minicom from hanging indefinitely on errors.

At 1 Mbaud (~100 KB/sec), 4 seconds allows uploading up to ~400KB firmware, which is more than sufficient for the 256KB SRAM target.

## Troubleshooting

**Issue: Connection fails**
- Verify correct serial port: `ls /dev/ttyUSB*` or `ls /dev/tty.usbserial*`
- Check FPGA is programmed with bitstream
- Ensure FAST bootloader is loaded (not standard bootloader)

**Issue: Upload hangs or times out**
- Verify 1 Mbaud baud rate: `src/minicom -D /dev/ttyUSB0 -b 1000000`
- FPGA must be running bootloader_fast or hexedit_fast firmware
- Standard bootloader won't work with FAST protocol
- Check `/tmp/minicom-fast-debug.log` for protocol trace

**Issue: CRC mismatch**
- Serial cable quality - try shorter cable
- Electromagnetic interference - check grounding
- Retry upload - transient errors are rare but possible

**Debug logging:** Check `/tmp/minicom-fast-debug.log` for detailed protocol trace showing all handshake steps and data transfer.

## Local Autopoint

The `.local-tools/` directory contains a locally-extracted autopoint binary and data files from the Ubuntu `autopoint_0.21-4ubuntu4_all.deb` package. This allows building without requiring system-wide installation of gettext tools.

## Source Code

Minicom-FPGA is based on minicom 2.10.90 from https://salsa.debian.org/minicom-team/minicom

All modifications are clearly marked and documented in the source code.

## License

GNU General Public License v2.0 or later (same as original minicom)

## Author

**Minicom-FPGA customization for PicoRV32:**
- Michael Wolak
- Email: mikewolak@gmail.com
- Email: mike@epromfoundry.com
- GitHub: https://github.com/mikewolak/olimex-ice40hx8k-picorv32
- Date: October 2025

**Original minicom:**
- Miquel van Smoorenburg and contributors
- https://salsa.debian.org/minicom-team/minicom
