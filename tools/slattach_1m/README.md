# slattach_1m - High-Speed SLIP Attach Tool

## Overview

Custom `slattach` implementation with support for high-speed serial baud rates up to 4 Mbaud. Standard system `slattach` typically only supports rates up to 115200 baud.

## Supported Baud Rates

- **Standard**: 9600, 19200, 38400, 57600, 115200
- **High-speed**: 230400, 460800, 500000, 576000, 921600
- **Very high-speed**: 1000000 (1M), 1152000, 1500000, 2000000, 2500000, 3000000, 3500000, 4000000

**Recommended for lwIP SLIP on FPGA**: 1000000 (1 Mbaud)

## Building

```bash
make
```

This produces the `slattach_1m` binary.

## Usage

Basic SLIP setup:
```bash
sudo ./slattach_1m -p slip -s 1000000 /dev/ttyUSB0 &
sudo ifconfig sl0 192.168.100.1 pointopoint 192.168.100.2 up
```

With 3-wire mode (no flow control - recommended for FPGA):
```bash
sudo ./slattach_1m -p slip -s 1000000 -L /dev/ttyUSB0 &
```

## Options

- `-p protocol` - Protocol type: `slip` or `cslip` (default: cslip)
- `-s speed` - Baud rate (e.g., 115200, 1000000, 2000000)
- `-L` - 3-wire mode (no hardware flow control)
- `-d` - Debug mode (verbose output)
- `-v` - Verbose mode
- `-V` - Show version

## Testing

Verify supported baud rates:
```bash
make test
```

## Performance

| Baud Rate | Theoretical | Real SLIP Throughput |
|-----------|-------------|---------------------|
| 115200    | 11.5 KB/s   | ~8-10 KB/s         |
| 1000000   | 100 KB/s    | ~75-85 KB/s        |
| 2000000   | 200 KB/s    | ~140-170 KB/s      |

## Hardware Requirements

**USB-to-Serial Adapter:**
- FTDI FT232R/FT232H: Up to 3 Mbaud ✓
- CP2102/CP2104: Up to 2 Mbaud ✓
- Prolific PL2303: Up to 921600 (varies by revision)

**FPGA Side:**
- UART configured for matching baud rate
- For 1 Mbaud @ 50 MHz: divisor = 49

## Example: lwIP SLIP on PicoRV32

1. Build firmware (if needed):
```bash
cd ../../firmware
make slip_echo_server
```

2. Upload to FPGA:
```bash
../tools/uploader/fw_upload -p /dev/ttyUSB0 slip_echo_server.bin
```

3. Start high-speed SLIP:
```bash
cd ../tools/slattach_1m
sudo ./slattach_1m -p slip -s 1000000 -L /dev/ttyUSB0 &
```

4. Configure interface:
```bash
sudo ifconfig sl0 192.168.100.1 pointopoint 192.168.100.2 up
```

5. Test:
```bash
ping -c 5 192.168.100.2
telnet 192.168.100.2 7777
```

6. Cleanup:
```bash
sudo killall slattach_1m
sudo ifconfig sl0 down
```

## Troubleshooting

**"Device or resource busy"**
```bash
sudo killall slattach slattach_1m
sudo ifconfig sl0 down
```

**"Cannot set baud rate"**
- Check USB adapter specifications
- Try lower baud rate (921600 or 1000000)
- Verify cable quality

**Packet loss at high speed**
- Use shorter USB cable
- Enable 3-wire mode with `-L` flag
- Check `dmesg | grep ttyUSB` for errors

## Source Code

Self-contained implementation based on net-tools 2.10 slattach.

Files:
- `slattach.c` - Main source (single file, ~450 lines)
- `Makefile` - Simple build system
- `README.md` - This file

## License

Based on net-tools, licensed under GPL v2.

## Author

Modified for high-speed support: Michael Wolak (mikewolak@gmail.com)
Original net-tools: Fred N. van Kempen et al.
Date: October 2025
