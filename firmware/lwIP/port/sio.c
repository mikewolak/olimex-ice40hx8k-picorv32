/*
 * Serial I/O Layer for lwIP SLIP on PicoRV32
 *
 * Provides sio_* functions needed by slipif.c
 * Maps to PicoRV32 UART at 0x80000000
 *
 * Copyright (c) October 2025 Michael Wolak
 */

#include "lwip/opt.h"
#include "lwip/sys.h"
#include "lwip/sio.h"
#include <stdint.h>

/*
 * UART Register Definitions
 * Base: 0x80000000 (MMIO)
 */
#define UART_TX_DATA   (*(volatile uint32_t*)0x80000000)
#define UART_TX_STATUS (*(volatile uint32_t*)0x80000004)
#define UART_RX_DATA   (*(volatile uint32_t*)0x80000008)
#define UART_RX_STATUS (*(volatile uint32_t*)0x8000000C)

/*
 * Status bits
 */
#define UART_TX_BUSY   (1 << 0)  /* TX busy (wait while high) */
#define UART_RX_AVAIL  (1 << 0)  /* RX data available (high = data ready) */

/*
 * Low-level UART functions
 */
static inline void uart_putc(uint8_t c) {
    while (UART_TX_STATUS & UART_TX_BUSY);
    UART_TX_DATA = c;
}

static inline int uart_getc_nonblock(void) {
    if (UART_RX_STATUS & UART_RX_AVAIL) {
        return UART_RX_DATA & 0xFF;
    }
    return -1;  /* No data available */
}

static inline uint8_t uart_getc_block(void) {
    while (!(UART_RX_STATUS & UART_RX_AVAIL));
    return UART_RX_DATA & 0xFF;
}

/*
 * sio_open - Open serial device
 *
 * For our system, we only have one UART, so we ignore device number.
 * Returns opaque handle (we just return a dummy non-NULL value).
 */
sio_fd_t sio_open(u8_t devnum)
{
    (void)devnum;  /* Ignore - we only have UART0 */

    /* UART already initialized by bootloader/startup code */

    /* Return dummy non-NULL handle */
    return (sio_fd_t)1;
}

/*
 * sio_send - Send single byte (blocking)
 *
 * Used by SLIP to transmit frames byte-by-byte
 */
void sio_send(u8_t c, sio_fd_t fd)
{
    (void)fd;
    uart_putc(c);
}

/*
 * sio_recv - Receive single byte (blocking)
 *
 * Used by SLIP RX thread (not used in NO_SYS mode)
 * Blocks until data is available
 */
u32_t sio_read(sio_fd_t fd, u8_t *data, u32_t len)
{
    (void)fd;
    u32_t i;

    for (i = 0; i < len; i++) {
        data[i] = uart_getc_block();
    }

    return len;
}

/*
 * sio_tryread - Non-blocking read
 *
 * Used by slipif_poll() in NO_SYS mode
 * Returns number of bytes actually read (0 if none available)
 */
u32_t sio_tryread(sio_fd_t fd, u8_t *data, u32_t len)
{
    (void)fd;
    u32_t i;
    int c;

    for (i = 0; i < len; i++) {
        c = uart_getc_nonblock();
        if (c < 0) {
            break;  /* No more data */
        }
        data[i] = (u8_t)c;
    }

    return i;  /* Return number of bytes read */
}

/*
 * sio_write - Write multiple bytes (blocking)
 *
 * Optional, but provided for completeness
 */
u32_t sio_write(sio_fd_t fd, const u8_t *data, u32_t len)
{
    (void)fd;
    u32_t i;

    for (i = 0; i < len; i++) {
        uart_putc(data[i]);
    }

    return len;
}

/*
 * sio_read_abort - Abort blocking read
 *
 * Not needed for NO_SYS mode, but must be defined
 */
void sio_read_abort(sio_fd_t fd)
{
    (void)fd;
    /* Nothing to do */
}
