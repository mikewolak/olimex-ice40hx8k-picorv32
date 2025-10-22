# SLIP Performance Test Server - Linux Version

## Purpose

This is a **local testing version** of the embedded lwIP performance test server. It implements the exact same protocol but runs as a standard Linux TCP server.

## Why This Exists

Testing the FPGA/lwIP version requires:
- Building firmware (~2 minutes)
- Uploading to FPGA
- Disconnecting terminal
- Starting SLIP on host
- Running test
- Reconnecting terminal to see logs
- **No debug output** during tests (UART is 100% SLIP)

This makes debugging **extremely slow**.

With the Linux version:
- Instant compilation (gcc, 1 second)
- Full printf debugging
- gdb support
- Test immediately
- **10x faster iteration**

## Usage

### Build
```bash
make
```

### Run Server
```bash
./slip_perf_server_linux
```

Server listens on port 8888 and shows detailed protocol debugging.

### Run Client (in another terminal)
```bash
cd ../../tools/slip_perf_client
./slip_perf_client 127.0.0.1 -d 2
```

## What You See

The server prints **every** message with full details:

```
[RX] Message type=0x03 length=4
[RX] Header bytes: 00 00 00 03 00 00 00 04
[RX] Payload bytes: 00 01 E0 00

=== TEST_START ===
Payload bytes: 00 01 E0 00
Requested block size: 122880 bytes (120 KB)
Server g_buffer_size: 245760 bytes (240 KB)
Comparison: 122880 > 245760 = FALSE (will accept)
SUCCESS: Sending TEST_ACK
```

## Debugging Protocol Issues

When the FPGA version fails, test the same scenario locally:

1. **Start Linux server** with full debug output
2. **Run same client test** that fails on FPGA
3. **See exact bytes received** in server output
4. **Compare to what FPGA should receive**

This immediately shows if the problem is:
- Client sending wrong data (unlikely - shown correct here)
- Protocol design issue (unlikely - works on Linux)
- **lwIP/embedded implementation** (most likely)

## Current Status

✅ **Linux version works perfectly**
- Receives correct block size (122,880 bytes)
- Protocol operates correctly
- No data corruption

❌ **lwIP version has data corruption**
- Same client, same test
- Server sees 3,146,240 bytes instead of 122,880
- Problem is in lwIP pbuf handling

## Next Steps

Use this to validate fixes to the lwIP version:
1. Fix lwIP pbuf handling in `firmware/slip_perf_server.c`
2. Test locally first (instant feedback)
3. Only upload to FPGA when local tests pass
4. Verify on hardware

## Files

- `slip_perf_server_linux.c` - Server implementation
- `Makefile` - Build system
- `README.md` - This file
