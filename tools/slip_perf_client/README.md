# SLIP Performance Test Client

Professional Linux client for testing TCP/IP performance over SLIP with the PicoRV32 firmware server.

## Features

- **Automatic Capability Negotiation** - Queries server for maximum buffer size
- **CRC32 Validation** - Ensures data integrity using 0xEDB88320 polynomial
- **Professional ncurses UI** - Real-time statistics display with progress bars
- **Bidirectional Testing** - Tests both send and receive performance
- **Timeout Protection** - Configurable timeouts prevent hanging
- **Clean Shutdown** - Signals server to return to idle state

## Building

```bash
make
```

Requires:
- gcc
- ncurses development library (`libncurses-dev` on Debian/Ubuntu)

## Usage

```bash
./slip_perf_client <server_ip> [options]
```

### Options

- `-d <seconds>` - Test duration (default: 2 seconds)
- `-t <seconds>` - Socket timeout (default: 1800 seconds / 30 minutes)
- `-b` - Enable bidirectional mode (default: unidirectional)

### Examples

Basic test (2 seconds, unidirectional):
```bash
./slip_perf_client 192.168.100.2
```

Extended test (10 seconds):
```bash
./slip_perf_client 192.168.100.2 -d 10
```

Bidirectional test:
```bash
./slip_perf_client 192.168.100.2 -d 2 -b
```

With custom timeout:
```bash
./slip_perf_client 192.168.100.2 -d 2 -t 30
```

## Complete Setup Example

### 1. Build and Upload Firmware

```bash
# In firmware directory
make slip_perf_server

# Upload to FPGA
../tools/uploader/fw_upload -p /dev/ttyUSB0 slip_perf_server.bin
```

### 2. Configure SLIP on Linux

```bash
# Start SLIP at 1 Mbaud
sudo ../slattach_1m/slattach_1m -p slip -s 1000000 -L /dev/ttyUSB0

# Configure interface (in another terminal)
sudo ifconfig sl0 192.168.100.1 pointopoint 192.168.100.2 up

# Verify connection
ping 192.168.100.2
```

### 3. Run Performance Test

```bash
./slip_perf_client 192.168.100.2 -d 2
```

## Display

The ncurses UI shows:

```
╔════════════════════════════════════════════════════════════════════════╗
║           SLIP Performance Test - Unidirectional Mode                  ║
╚════════════════════════════════════════════════════════════════════════╝

  Server Buffer:  120 KB
  Block Size:     120 KB
  Test Duration:  2 seconds

  Progress:       2 / 2 seconds
  [############################################################] 100%

  TRANSMIT (Client → Server)
    Packets:           50
    Bytes:       6144000  ( 6000.00 KB)
    Rate:          3000.00 KB/s

  RECEIVE (Server → Client)
    Packets:           50
    Bytes:       6144000  ( 6000.00 KB)
    Rate:          3000.00 KB/s

  ERRORS:              0

  Status: Complete - Press Ctrl-C to exit
```

## Protocol

The client implements the same protocol as the firmware server:

### Message Types

| Type | Name | Description |
|------|------|-------------|
| 0x01 | CAPS_REQ | Request server capabilities |
| 0x02 | CAPS_RESP | Server responds with max buffer size |
| 0x03 | TEST_START | Start test with specified block size |
| 0x04 | TEST_ACK | Server acknowledges test start |
| 0x05 | DATA_CRC | CRC32 value for following data block |
| 0x06 | DATA_BLOCK | Actual data block |
| 0x07 | DATA_ACK | Acknowledge data block received |
| 0x08 | TEST_STOP | Stop test and return to idle |
| 0xFF | ERROR | Error occurred |

### Message Format

All messages have an 8-byte header:

```
[Type:4 bytes][Length:4 bytes][Payload:N bytes]
```

### Test Sequence

1. Client connects to server on port 8888
2. Client sends `CAPS_REQ`
3. Server responds with `CAPS_RESP` containing max buffer size
4. Client sends `TEST_START` with desired block size
5. Server responds with `TEST_ACK` (or `ERROR` if invalid)
6. **Data exchange loop:**
   - Client generates random data
   - Client calculates CRC32
   - Client sends `DATA_CRC` + `DATA_BLOCK`
   - Server validates CRC32
   - Server generates random data
   - Server calculates CRC32
   - Server sends `DATA_CRC` + `DATA_BLOCK`
   - Client validates CRC32
   - Repeat until duration expires
7. Client sends `TEST_STOP`
8. Server returns to idle state

## CRC32 Implementation

Uses the same polynomial as firmware (0xEDB88320):
- Standard Ethernet/ZIP CRC32
- Matches `firmware/hexedit.c` implementation
- Ensures interoperability with firmware

## Performance Expectations

At 1 Mbaud SLIP:
- Theoretical max: ~125 KB/s (1,000,000 bits/sec / 8)
- Practical throughput: ~75-85 KB/s (accounting for SLIP framing overhead)
- Latency: ~2-5ms (ping)

With 120 KB blocks:
- ~1 round-trip per second
- ~150 KB/s combined (75 KB/s each direction)

## Troubleshooting

### "Connection refused"
- Verify firmware is running: Check UART output shows "Performance test server listening on port 8888"
- Verify SLIP is configured: `ifconfig sl0` should show UP,POINTOPOINT,RUNNING
- Verify routing: `ping 192.168.100.2` should work

### "Failed to request capabilities"
- Check socket timeout (-t option)
- Verify firmware is responding
- Check for network errors in SLIP

### "CRC mismatch"
- Indicates data corruption
- Check UART baud rate matches (1000000)
- Check UART signal quality
- Try reducing block size

### Display garbled after exit
If ncurses doesn't clean up properly on error, reset terminal:
```bash
reset
```

## Author

Michael Wolak (mikewolak@gmail.com, mike@epromfoundry.com)

October 2025
