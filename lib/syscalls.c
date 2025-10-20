//===============================================================================
// Minimal RISC-V Syscalls for UART stdio
// Provides _read(), _write() syscalls for printf/scanf support
//
// Copyright (c) October 2025 Michael Wolak
// Email: mikewolak@gmail.com, mike@epromfoundry.com
//===============================================================================

#include <errno.h>

// FreeRTOS support for thread-safe newlib
#ifdef USE_FREERTOS
#include <FreeRTOS.h>
#include <semphr.h>
#include <task.h>
#include <sys/reent.h>

static SemaphoreHandle_t uart_mutex = NULL;

// Initialize UART mutex (call this before starting scheduler)
void syscalls_init_uart_mutex(void) {
    if (uart_mutex == NULL) {
        uart_mutex = xSemaphoreCreateMutex();
    }
}

//===============================================================================
// Newlib Reentrant Locking Functions
// Required for thread-safe malloc, environment variables, etc.
//===============================================================================

void __malloc_lock(struct _reent *reent) {
    (void)reent;
    vTaskSuspendAll();
}

void __malloc_unlock(struct _reent *reent) {
    (void)reent;
    xTaskResumeAll();
}

void __env_lock(struct _reent *reent) {
    (void)reent;
    vTaskSuspendAll();
}

void __env_unlock(struct _reent *reent) {
    (void)reent;
    xTaskResumeAll();
}

#endif

// Minimal definitions (normally from sys/stat.h)
#ifndef EBADF
#define EBADF  9
#endif
#ifndef ENOMEM
#define ENOMEM 12
#endif
#ifndef EINVAL
#define EINVAL 22
#endif

#define S_IFCHR  0020000  // Character device

struct stat {
    unsigned short st_mode;
};

// errno variable - don't define when using full newlib (it provides errno)
// Only define for minimal bare-metal builds
#if 0
int errno;
#endif

// UART Register Definitions
#define UART_TX_DATA   (*(volatile unsigned int*)0x80000000)
#define UART_TX_STATUS (*(volatile unsigned int*)0x80000004)
#define UART_RX_DATA   (*(volatile unsigned int*)0x80000008)
#define UART_RX_STATUS (*(volatile unsigned int*)0x8000000C)

//===============================================================================
// Low-level UART functions
//===============================================================================

static void uart_putc(char c) {
    // Wait for TX ready
    while (UART_TX_STATUS & 0x01);
    UART_TX_DATA = c;
}

static char uart_getc(void) {
    // Wait for RX data available (bit is 1 when data available)
    while (!(UART_RX_STATUS & 0x01));
    return UART_RX_DATA & 0xFF;
}

//===============================================================================
// Syscall: _write
// Used by printf(), puts(), etc.
//===============================================================================

int _write(int file, char *ptr, int len) {
    int written = 0;

    // Only support stdout (fd 1) and stderr (fd 2)
    if (file != 1 && file != 2) {
        errno = EBADF;
        return -1;
    }

#ifdef USE_FREERTOS
    // Take mutex to protect UART access in multithreaded environment
    if (uart_mutex != NULL) {
        xSemaphoreTake(uart_mutex, portMAX_DELAY);
    }
#endif

    // Write each character to UART
    for (int i = 0; i < len; i++) {
        uart_putc(*ptr++);
        written++;
    }

#ifdef USE_FREERTOS
    // Release mutex
    if (uart_mutex != NULL) {
        xSemaphoreGive(uart_mutex);
    }
#endif

    return written;
}

//===============================================================================
// Syscall: _read
// Used by scanf(), getchar(), etc.
//===============================================================================

int _read(int file, char *ptr, int len) {
    int read = 0;

    // Only support stdin (fd 0)
    if (file != 0) {
        errno = EBADF;
        return -1;
    }

    // Read characters from UART
    for (int i = 0; i < len; i++) {
        char c = uart_getc();

        // Echo character (optional, comment out if not desired)
        uart_putc(c);

        // Handle newline
        if (c == '\r') {
            c = '\n';
            uart_putc('\n');  // Echo newline
        }

        *ptr++ = c;
        read++;

        // Stop at newline for line-buffered input
        if (c == '\n') {
            break;
        }
    }

    return read;
}

//===============================================================================
// Syscall: _close
//===============================================================================

int _close(int file) {
    (void)file;
    return -1;
}

//===============================================================================
// Syscall: _lseek
//===============================================================================

int _lseek(int file, int offset, int whence) {
    (void)file;
    (void)offset;
    (void)whence;
    return 0;
}

//===============================================================================
// Syscall: _fstat
//===============================================================================

int _fstat(int file, struct stat *st) {
    (void)file;
    st->st_mode = S_IFCHR;  // Character device
    return 0;
}

//===============================================================================
// Syscall: _isatty
//===============================================================================

int _isatty(int file) {
    (void)file;
    return 1;  // All files are "tty" (UART)
}

//===============================================================================
// Syscall: _sbrk (heap management)
// Required for malloc() support
//===============================================================================

extern char __heap_start;  // Defined in linker script
extern char __heap_end;    // Defined in linker script

static char *heap_ptr = &__heap_start;

void *_sbrk(int incr) {
    char *prev_heap_ptr = heap_ptr;

    // Check if we would exceed heap
    if (heap_ptr + incr > &__heap_end) {
        errno = ENOMEM;
        return (void *)-1;
    }

    heap_ptr += incr;
    return (void *)prev_heap_ptr;
}

//===============================================================================
// Syscall: _kill
//===============================================================================

int _kill(int pid, int sig) {
    (void)pid;
    (void)sig;
    errno = EINVAL;
    return -1;
}

//===============================================================================
// Syscall: _getpid
//===============================================================================

int _getpid(void) {
    return 1;
}

//===============================================================================
// Syscall: _exit
// Called when program exits
//===============================================================================

void _exit(int status) {
    (void)status;
    // Infinite loop - no operating system to return to
    while (1) {
        __asm__ volatile ("wfi");  // Wait for interrupt
    }
}
