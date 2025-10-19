/*
 * FreeRTOS Configuration for PicoRV32
 *
 * This file uses CONFIG_* variables from Kconfig/.config
 * These must be passed as -D flags during compilation
 */

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stdint.h>

/* CPU and Clock - from Kconfig */
#define configCPU_CLOCK_HZ              CONFIG_FREERTOS_CPU_CLOCK_HZ
#define configTICK_RATE_HZ              CONFIG_FREERTOS_TICK_RATE_HZ

/* Task Configuration - from Kconfig */
#define configMAX_PRIORITIES            CONFIG_FREERTOS_MAX_PRIORITIES
#define configMINIMAL_STACK_SIZE        CONFIG_FREERTOS_MINIMAL_STACK_SIZE
#define configMAX_TASK_NAME_LEN         16

/* Memory - from Kconfig */
#define configTOTAL_HEAP_SIZE           CONFIG_FREERTOS_TOTAL_HEAP_SIZE
#define configSUPPORT_DYNAMIC_ALLOCATION 1
#define configSUPPORT_STATIC_ALLOCATION 0

/* Kernel Features */
#define configUSE_PREEMPTION            1
#define configUSE_TIME_SLICING          1
#define configUSE_16_BIT_TICKS          0
#define configUSE_MUTEXES               1
#define configUSE_COUNTING_SEMAPHORES   1

/* Hook Functions */
#define configUSE_IDLE_HOOK             1
#define configUSE_TICK_HOOK             0
#define configUSE_MALLOC_FAILED_HOOK    1

/* Optional Functions - from Kconfig */
#ifdef CONFIG_FREERTOS_INCLUDE_vTaskDelay
#define INCLUDE_vTaskDelay              1
#else
#define INCLUDE_vTaskDelay              0
#endif

#ifdef CONFIG_FREERTOS_INCLUDE_vTaskDelayUntil
#define INCLUDE_vTaskDelayUntil         1
#else
#define INCLUDE_vTaskDelayUntil         0
#endif

#ifdef CONFIG_FREERTOS_INCLUDE_vTaskDelete
#define INCLUDE_vTaskDelete             1
#else
#define INCLUDE_vTaskDelete             0
#endif

#ifdef CONFIG_FREERTOS_INCLUDE_xTaskGetCurrentTaskHandle
#define INCLUDE_xTaskGetCurrentTaskHandle 1
#else
#define INCLUDE_xTaskGetCurrentTaskHandle 0
#endif

#ifdef CONFIG_FREERTOS_INCLUDE_uxTaskPriorityGet
#define INCLUDE_uxTaskPriorityGet       1
#else
#define INCLUDE_uxTaskPriorityGet       0
#endif

#ifdef CONFIG_FREERTOS_INCLUDE_uxTaskGetStackHighWaterMark
#define INCLUDE_uxTaskGetStackHighWaterMark 1
#else
#define INCLUDE_uxTaskGetStackHighWaterMark 0
#endif

/* Newlib reentrant support */
#define configUSE_NEWLIB_REENTRANT      1

/* PicoRV32-specific: No standard RISC-V MTIME/MTIMECMP */
#define configMTIME_BASE_ADDRESS        0
#define configMTIMECMP_BASE_ADDRESS     0

/* Assertions */
#define configASSERT(x) if((x) == 0) { __asm__ volatile("ebreak"); for(;;); }

#endif /* FREERTOS_CONFIG_H */
