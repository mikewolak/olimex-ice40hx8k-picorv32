# SPI Burst Transfer Implementation Summary

## Overview
Implemented hardware-accelerated SPI burst transfers using a byte counter approach for improved SD card I/O performance.

## Expected Performance Improvement
- **Baseline**: ~60 KB/s (byte-by-byte transfers)
- **Optimized**: ~166 KB/s (512-byte burst transfers)
- **Improvement**: ~2.8x speedup

## Hardware Changes

### Modified: `hdl/spi_master.v`
Added burst transfer support with minimal LUT overhead:

**New Registers**:
- `reg [12:0] burst_count` - Tracks remaining bytes in burst (0-8192)
- `reg burst_mode` - Indicates burst mode active

**New Memory-Mapped Register**:
- `ADDR_SPI_BURST` (0x80000060) - Write burst count, read remaining count

**Modified State Machine**:
- `STATE_FINISH` now checks `burst_mode` and `burst_count`
- If burst active and bytes remain, continues to `STATE_IDLE` for next byte
- Single IRQ pulse generated only at burst completion

**Status Register Enhancement**:
- Bit 2: `SPI_STATUS_BURST_MODE` - Indicates burst mode active

**Resource Usage**:
- Baseline: 6913 LUTs (90%)
- With burst mode: 6895 LUTs (89%)
- **Net change: -18 LUTs** (optimizer improvements)

## Firmware Changes

### Modified: `firmware/sd_fatfs/hardware.h`
Added burst mode register definitions:
```c
#define SPI_BURST       (*(volatile uint32_t*)(SPI_BASE + 0x10))
#define SPI_STATUS_BURST_MODE (1 << 2)
```

### Modified: `firmware/sd_fatfs/io.h`
Added burst transfer function declaration:
```c
void spi_burst_transfer(const uint8_t *tx_buf, uint8_t *rx_buf, uint32_t count);
```

### Modified: `firmware/sd_fatfs/io.c`
Added burst transfer implementation:
- Sets `SPI_BURST` register to enable burst mode
- Loops through all bytes with normal SPI_DATA writes
- Waits for `SPI_STATUS_BURST_MODE` to clear at end

### Created: `firmware/sd_fatfs/sd_spi_optimized.c`
Optimized SD card SPI driver using burst transfers:
- `sd_read_block()`: Uses `spi_burst_transfer(NULL, buffer, 512)` for 512-byte reads
- `sd_write_block()`: Uses `spi_burst_transfer(buffer, NULL, 512)` for 512-byte writes
- Header updated to indicate optimized version

### Created: `firmware/sd_fatfs/sd_card_manager_optimized.c`
Copy of `sd_card_manager.c` that will use optimized SD SPI driver via build system

### Modified: `firmware/Makefile`
Added build support for optimized version:

**Target List**:
```makefile
SD_FATFS_TARGETS = sd_card_manager sd_card_manager_optimized
```

**Object Selection**:
- When building `sd_card_manager_optimized`:
  - Uses `sd_card_manager_optimized.o` and `sd_spi_optimized.o`
- When building `sd_card_manager`:
  - Uses `sd_card_manager.o` and `sd_spi.o` (baseline)

**Build Targets**:
```makefile
sd_card_manager:
	$(MAKE) TARGET=sd_card_manager USE_SD_FATFS=1 USE_NEWLIB=1 single-target

sd_card_manager_optimized:
	$(MAKE) TARGET=sd_card_manager_optimized USE_SD_FATFS=1 USE_NEWLIB=1 single-target
```

## Building and Testing

### Build Baseline Version
```bash
cd firmware
make sd_card_manager
```

### Build Optimized Version
```bash
cd firmware
make sd_card_manager_optimized
```

### Upload to FPGA
```bash
# Upload hardware bitstream (includes burst mode support)
cd ..
make prog

# Upload firmware
cd firmware
../tools/uploader/fw_upload_fast sd_card_manager_optimized.bin
```

### Testing
The optimized version should show:
1. Same functionality as baseline (backward compatible)
2. ~2.8x faster SD card read/write operations
3. Noticeable improvement in file operations and directory listings

## Design Rationale

### Why Byte Counter vs FIFO?
1. **512-byte FIFO approach**: Required 7053 LUTs (91%) - FAILED placement
2. **Byte counter approach**: Required 6895 LUTs (89%) - SUCCESS
3. **LUT savings**: 158 LUTs less than FIFO approach
4. **BRAM savings**: No BRAM blocks needed (vs 2 blocks for FIFO)

### Backward Compatibility
- When `burst_count = 0`: Behaves exactly like baseline (single-byte mode)
- Existing firmware continues to work without modification
- Optimized firmware uses burst mode only for 512-byte block transfers

## Files Modified/Created

### Hardware
- `hdl/spi_master.v` (modified)

### Firmware
- `firmware/sd_fatfs/hardware.h` (modified)
- `firmware/sd_fatfs/io.h` (modified)
- `firmware/sd_fatfs/io.c` (modified)
- `firmware/sd_fatfs/sd_spi_optimized.c` (created)
- `firmware/sd_fatfs/sd_card_manager_optimized.c` (created)
- `firmware/Makefile` (modified)

### Documentation
- `SPI_BURST_IMPLEMENTATION_SUMMARY.md` (this file)

## Implementation Notes

1. **Burst count is 13 bits**: Supports 0-8192 bytes
2. **Firmware manages buffering**: No hardware FIFOs required
3. **Single IRQ at completion**: Reduces interrupt overhead
4. **Optimal for SD cards**: 512-byte blocks match SD card sector size
5. **Minimal hardware cost**: Only ~20 LUTs for counter + control logic

## Next Steps

1. Test baseline vs optimized versions on hardware
2. Benchmark actual performance improvement
3. Consider adding performance metrics to UI
4. Potentially use burst mode for other operations (multi-block reads/writes)

## Author
Michael Wolak (mikewolak@gmail.com, mike@epromfoundry.com)
Date: October 2025
