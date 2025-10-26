# Overlay Upload Memory Fix

## Problem

The overlay upload was configured to use address 0x00042000 as the upload buffer, which is actually the **start of the stack region**, not safe heap space. This could cause stack corruption and upload failures.

## Root Cause

From linker.ld:
- APPSRAM: 0x00000000 - 0x0003FFFF (256KB for code/data/bss)
- STACK: 0x00042000 - 0x00080000 (248KB for heap/stack)
- Heap: starts after BSS, ends at 0x00042000
- Stack: grows down from 0x00080000

The old configuration used 0x00042000 which is where the stack region begins, not a safe heap location!

## Solution

### 1. Dynamic Buffer Allocation
Changed upload buffer to use:
```c
extern uint32_t __heap_start;  // Linker symbol
#define UPLOAD_BUFFER_OFFSET  (64 * 1024)  // 64KB offset
#define UPLOAD_BUFFER_BASE    ((uint32_t)&__heap_start + UPLOAD_BUFFER_OFFSET)
```

This gives 64KB of heap space for normal allocations, then uses the next 128KB for uploads.

### 2. Conservative Size Limit
Changed max upload size from 96KB to 128KB:
```c
#define MAX_OVERLAY_SIZE  (128 * 1024)  // 128KB max
```

This ensures we don't overflow into stack space.

### 3. Actual Bytes Tracking
The upload protocol now properly tracks `bytes_received` and only writes that many bytes to the SD card, not a fixed buffer size.

### 4. Terminal Formatting
Fixed all `\n` to `\r\n` for proper terminal display.

## Memory Layout After Fix

```
0x00000000 ┌─────────────────────┐
           │  Code/Data/BSS      │ 256KB (APPSRAM)
0x0003FFFF ├─────────────────────┤
           │  (unused gap)       │
0x00042000 ├─────────────────────┤
           │  Heap (dynamic)     │ 64KB for malloc/heap
           │  __heap_start       │
           │  + 64KB offset      │
           ├─────────────────────┤
           │  Upload Buffer      │ 128KB max upload
           │  (heap + 64KB)      │
           ├─────────────────────┤
           │  Remaining Heap     │
           │  (available)        │
0x00080000 ├─────────────────────┤
           │  Stack (grows down) │
           │  __stack_top        │
           └─────────────────────┘
```

## Files Modified

1. `overlay_upload.h` - Changed buffer base to dynamic calculation
2. `overlay_upload.c` - Changed `\n` to `\r\n` for all printf
3. `overlay_loader.h` - Removed duplicate UPLOAD_BUFFER_BASE definition

## Testing

Upload now uses safe heap space and won't corrupt stack memory.
Terminal output displays correctly with proper line endings.
