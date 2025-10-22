/*
 * Architecture-specific definitions for lwIP on PicoRV32
 *
 * Compiler: GCC for RISC-V
 * Platform: PicoRV32 RV32IM
 */

#ifndef LWIP_ARCH_CC_H
#define LWIP_ARCH_CC_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Data type definitions for lwIP
 */
typedef uint8_t     u8_t;
typedef int8_t      s8_t;
typedef uint16_t    u16_t;
typedef int16_t     s16_t;
typedef uint32_t    u32_t;
typedef int32_t     s32_t;

typedef uintptr_t   mem_ptr_t;

/*
 * Critical section protection type (NO_SYS mode)
 * Used to save/restore interrupt state
 */
typedef u32_t       sys_prot_t;

/*
 * Compiler hints for lwIP
 */
#define PACK_STRUCT_FIELD(x)    x
#define PACK_STRUCT_STRUCT      __attribute__((packed))
#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_END

/*
 * Platform-specific diagnostic output
 * Defined in lwipopts.h to use printf
 */
#ifndef LWIP_PLATFORM_DIAG
#define LWIP_PLATFORM_DIAG(x) do { printf x; } while(0)
#endif

#ifndef LWIP_PLATFORM_ASSERT
#define LWIP_PLATFORM_ASSERT(x) do { \
    printf("LWIP ASSERT: %s\n", x); \
    while(1); \
} while(0)
#endif

/*
 * Random number generator (simple for now)
 */
#define LWIP_RAND() ((u32_t)rand())

/*
 * Byte order (RISC-V is little-endian)
 */
#define BYTE_ORDER LITTLE_ENDIAN

#endif /* LWIP_ARCH_CC_H */
