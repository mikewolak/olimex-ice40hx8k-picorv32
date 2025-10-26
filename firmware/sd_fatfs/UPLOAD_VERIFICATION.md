# Upload Protocol Verification - NO Printf During Transfer

## Critical Rule

**NEVER call printf() during UART upload protocol!**

Printf uses the same UART as the upload protocol, which corrupts the data stream
and causes terminal crashes.

## Verified Upload Implementations

### 1. overlay_upload.c - overlay_upload()
**Status**: ✅ VERIFIED SAFE

Protocol flow:
```c
// Step 1-3: Print banner BEFORE protocol
printf("Step 1: Waiting for 'R' command...\r\n");  // ✅ Before transfer
printf("Step 2: Sent 'A' (ready ACK)\r\n");        // ✅ Before transfer
printf("Step 3: Receiving size...\r\n");           // ✅ Before transfer

// Step 4: Validate size
if (packet_size > MAX_SIZE) {
    printf("Error: Invalid size...\r\n");          // ✅ Error exit OK
    return;
}

// Step 5: STREAM DATA - NO PRINTF!
// NOTE: NO printf during transfer! It uses the same UART
while (bytes_received < packet_size) {
    buffer[bytes_received] = uart_getc_raw();      // ✅ No printf
    // LED toggles only for progress
}

// Step 6: Calculate CRC - NO PRINTF!
// NOTE: Still no printf - protocol not complete yet!
calculated_crc = calculate_crc32(...);             // ✅ No printf

// Step 7-9: Exchange CRC - NO PRINTF!
uart_getc_raw();  // Get 'C' command                // ✅ No printf
uart_getc_raw();  // Get expected CRC               // ✅ No printf
uart_putc_raw('C');  // Send CRC response           // ✅ No printf

// Step 10: PROTOCOL COMPLETE - NOW print results
printf("\r\n");                                     // ✅ After protocol
printf("*** Upload SUCCESS ***\r\n");               // ✅ After protocol
printf("Received: %lu bytes\r\n", ...);             // ✅ After protocol
printf("CRC32: 0x%08lX\r\n", ...);                  // ✅ After protocol

// Step 11: Save to SD - safe to print
printf("Step 11: Saving to SD card...\r\n");        // ✅ After protocol
```

### 2. overlay_upload.c - overlay_upload_and_execute()
**Status**: ✅ VERIFIED SAFE

Same protocol pattern as overlay_upload() - all printf calls are:
- Before protocol starts (banner/setup)
- After protocol completes (results)
- Error cases (protocol aborted)

### 3. hexedit_fast.c - cmd_simple_upload()
**Status**: ✅ VERIFIED SAFE (reference implementation)

From hexedit_fast.c line 418:
```c
// NOTE: NO debug messages during protocol! They interfere with data stream
```

This is the proven working implementation that overlay_upload.c now matches.

### 4. hexedit.c - cmd_simple_upload()
**Status**: ✅ SAFE (uses library function)

Uses simple_receive() library function which handles protocol internally.
No direct printf during transfer.

### 5. sd_card_manager.c
**Status**: ✅ SAFE (wrapper only)

Only calls overlay_upload() and overlay_upload_and_execute() functions.
No direct UART protocol implementation.

## Protocol Safety Checklist

For ANY UART upload protocol implementation:

- [ ] Banner/setup printed BEFORE protocol starts
- [ ] NO printf during data streaming
- [ ] NO printf during CRC calculation
- [ ] NO printf during CRC exchange
- [ ] Results printed AFTER protocol completes
- [ ] Error messages only on protocol abort
- [ ] LED toggles for progress indication (not printf)

## Memory Safety Verification

Upload buffer location:
- Base: `__heap_start + 64KB` (dynamic, safe heap location)
- Size: 128KB maximum
- Location: Between heap allocations and stack
- Does NOT overlap with stack region (0x00042000 - 0x00080000)

## Testing

All upload implementations tested and verified:
- No terminal crashes
- Data integrity maintained (CRC verification passes)
- Proper error reporting after protocol completes
- Clean separation: printf before/after, UART during

## Reference

Working example: hexedit_fast.c lines 362-479
Critical comment: line 418 "NO debug messages during protocol!"
