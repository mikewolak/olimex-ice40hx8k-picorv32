/*
 * FreeRTOS PicoRV32 Port - portmacro.h
 *
 * Port-specific type definitions and macros for PicoRV32
 */

#ifndef PORTMACRO_H
#define PORTMACRO_H

#include <stdint.h>

/* Type definitions for RV32I */
#define portSTACK_TYPE      uint32_t
#define portBASE_TYPE       int32_t
#define portUBASE_TYPE      uint32_t
#define portMAX_DELAY       (0xffffffffUL)

typedef portSTACK_TYPE   StackType_t;
typedef portBASE_TYPE    BaseType_t;
typedef portUBASE_TYPE   UBaseType_t;
typedef portUBASE_TYPE   TickType_t;

#define portCHAR        char
#define portFLOAT       float
#define portDOUBLE      double
#define portLONG        long
#define portSHORT       short

/* 32-bit tick type on 32-bit architecture */
#define portTICK_TYPE_IS_ATOMIC    1

/* Architecture specifics */
#define portSTACK_GROWTH          (-1)
#define portTICK_PERIOD_MS        ((TickType_t) 1000 / configTICK_RATE_HZ)
#define portBYTE_ALIGNMENT        16

/* Scheduler utilities */
extern void vTaskSwitchContext(void);

/* Yield using PicoRV32 software interrupt would go here if we had it
 * For now, we'll handle yields in the timer interrupt */
#define portYIELD()     /* Handled by timer interrupt */

/* Critical section management using PicoRV32 IRQ masking */
#define portDISABLE_INTERRUPTS()    picorv32_maskirq(~0)
#define portENABLE_INTERRUPTS()     picorv32_maskirq(0)

/* Critical section entry/exit (implemented in port.c) */
extern void vPortEnterCritical(void);
extern void vPortExitCritical(void);
#define portENTER_CRITICAL()    vPortEnterCritical()
#define portEXIT_CRITICAL()     vPortExitCritical()

/* PicoRV32 IRQ control inline functions */
static inline uint32_t picorv32_maskirq(uint32_t mask) {
    uint32_t old_mask;
    __asm__ volatile (".insn r 0x0B, 6, 3, %0, %1, x0" : "=r"(old_mask) : "r"(mask));
    return old_mask;
}

static inline uint32_t picorv32_getirq(void) {
    uint32_t irqs;
    __asm__ volatile (".insn r 0x0B, 4, 0, %0, x1, x0" : "=r"(irqs));
    return irqs;
}

/* Task function macros */
#define portTASK_FUNCTION_PROTO(vFunction, pvParameters) void vFunction(void *pvParameters)
#define portTASK_FUNCTION(vFunction, pvParameters) void vFunction(void *pvParameters)

/* Architecture-specific optimizations */
#define portNOP()    __asm volatile ("nop")

#endif /* PORTMACRO_H */
