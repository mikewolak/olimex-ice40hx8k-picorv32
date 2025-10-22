//===============================================================================
// TCP Echo Server over SLIP - lwIP Demo for PicoRV32
//
// Demonstrates TCP/IP networking over serial line (SLIP protocol)
//
// Features:
// - lwIP stack in NO_SYS mode (bare metal, no RTOS)
// - SLIP interface over UART (1000000 baud / 1 Mbaud)
// - TCP echo server on port 7777
// - ICMP (ping) support
//
// Linux Host Setup:
//   sudo tools/slattach_1m/slattach_1m -p slip -s 1000000 -L /dev/ttyUSB0 &
//   sudo ifconfig sl0 192.168.100.1 pointopoint 192.168.100.2 up
//   ping 192.168.100.2
//   telnet 192.168.100.2 7777
//
// Copyright (c) October 2025 Michael Wolak
// Email: mikewolak@gmail.com, mike@epromfoundry.com
//===============================================================================

#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* lwIP core includes */
#include "lwip/opt.h"
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/timeouts.h"
#include "lwip/ip_addr.h"
#include "lwip/stats.h"

/* lwIP SLIP interface */
#include "netif/slipif.h"

/* lwIP TCP API */
#include "lwip/tcp.h"

//==============================================================================
// Configuration
//==============================================================================

#define DEVICE_IP       "192.168.100.2"    /* This device (PicoRV32) */
#define GATEWAY_IP      "192.168.100.1"    /* Linux host */
#define NETMASK         "255.255.255.0"

#define ECHO_PORT       7777                /* TCP echo server port */
#define FLUSH_THRESHOLD 128                 /* Flush at 128 bytes or LF */

//==============================================================================
// LED Control (for activity indication)
//==============================================================================

#define LED_CONTROL (*(volatile uint32_t*)0x80000010)

static void led_set(uint8_t state) {
    LED_CONTROL = state;
}

//==============================================================================
// TCP Echo Server State
//==============================================================================

struct echo_state {
    struct tcp_pcb *pcb;
    uint32_t bytes_received;
    uint32_t bytes_sent;
    uint16_t unflushed_bytes;       /* Bytes written since last tcp_output() */
};

//==============================================================================
// TCP Echo Server Callbacks
//==============================================================================

/*
 * Error callback - called when connection error occurs
 */
static void echo_err(void *arg, err_t err)
{
    struct echo_state *es = (struct echo_state *)arg;

    printf("TCP Error: %d\r\n", err);

    if (es != NULL) {
        free(es);
    }
}

/*
 * Receive callback - called when data arrives
 */
static err_t echo_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    struct echo_state *es = (struct echo_state *)arg;
    err_t ret_err;

    /* Check for connection closed by remote host */
    if (p == NULL) {
        printf("Connection closed by peer\r\n");
        tcp_close(tpcb);
        if (es != NULL) {
            free(es);
        }
        return ERR_OK;
    }

    /* Handle error */
    if (err != ERR_OK) {
        if (p != NULL) {
            pbuf_free(p);
        }
        return err;
    }

    /* Update received byte count */
    es->bytes_received += p->tot_len;

    /* Echo data back - handle pbuf chain with smart flushing */
    struct pbuf *q;
    ret_err = ERR_OK;

    for (q = p; q != NULL && ret_err == ERR_OK; q = q->next) {
        uint8_t *data = (uint8_t *)q->payload;
        uint16_t len = q->len;
        uint16_t start = 0;

        /* Scan for line feeds to determine flush points */
        for (uint16_t i = 0; i < len && ret_err == ERR_OK; i++) {
            int need_flush = 0;

            /* Check for line feed */
            if (data[i] == '\n') {
                need_flush = 1;
            }
            /* Check if we've accumulated 128 bytes */
            else if (es->unflushed_bytes + (i - start + 1) >= FLUSH_THRESHOLD) {
                need_flush = 1;
            }

            /* Time to flush? */
            if (need_flush || i == len - 1) {
                uint16_t chunk_len = i - start + 1;

                ret_err = tcp_write(tpcb, data + start, chunk_len, TCP_WRITE_FLAG_COPY);
                if (ret_err == ERR_OK) {
                    es->bytes_sent += chunk_len;
                    es->unflushed_bytes += chunk_len;

                    if (need_flush) {
                        tcp_output(tpcb);
                        es->unflushed_bytes = 0;
                    }
                }
                start = i + 1;
            }
        }
    }

    if (ret_err == ERR_OK) {
        /* Blink LED on activity */
        led_set(es->bytes_received & 0x100 ? 1 : 0);

        printf("Echo: %u bytes (total RX=%lu, TX=%lu)\r\n",
               p->tot_len, (unsigned long)es->bytes_received, (unsigned long)es->bytes_sent);
    } else {
        printf("TCP write error: %d\r\n", ret_err);
    }

    /* Tell TCP we processed the data */
    tcp_recved(tpcb, p->tot_len);

    /* Final flush if any data remains unflushed */
    if (es->unflushed_bytes > 0) {
        tcp_output(tpcb);
        es->unflushed_bytes = 0;
    }

    /* Free the pbuf */
    pbuf_free(p);

    return ERR_OK;
}

/*
 * Accept callback - called when new connection arrives
 */
static err_t echo_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    struct echo_state *es;

    (void)arg;

    if (err != ERR_OK || newpcb == NULL) {
        return ERR_VAL;
    }

    printf("\r\n=== New connection from %s:%u ===\r\n",
           ipaddr_ntoa(&newpcb->remote_ip), newpcb->remote_port);

    /* Allocate state for this connection */
    es = (struct echo_state *)malloc(sizeof(struct echo_state));
    if (es == NULL) {
        printf("Out of memory!\r\n");
        return ERR_MEM;
    }

    es->pcb = newpcb;
    es->bytes_received = 0;
    es->bytes_sent = 0;
    es->unflushed_bytes = 0;

    /* Set up TCP callbacks */
    tcp_arg(newpcb, es);
    tcp_recv(newpcb, echo_recv);
    tcp_err(newpcb, echo_err);

    return ERR_OK;
}

/*
 * Initialize TCP echo server
 */
static void echo_server_init(void)
{
    struct tcp_pcb *pcb;

    /* Create new TCP PCB */
    pcb = tcp_new();
    if (pcb == NULL) {
        printf("Failed to create TCP PCB!\r\n");
        return;
    }

    /* Bind to echo port */
    err_t err = tcp_bind(pcb, IP_ADDR_ANY, ECHO_PORT);
    if (err != ERR_OK) {
        printf("TCP bind failed: %d\r\n", err);
        tcp_close(pcb);
        return;
    }

    /* Start listening */
    pcb = tcp_listen(pcb);
    if (pcb == NULL) {
        printf("TCP listen failed!\r\n");
        return;
    }

    /* Set accept callback */
    tcp_accept(pcb, echo_accept);

    printf("TCP echo server listening on port %d\r\n", ECHO_PORT);
}

//==============================================================================
// Network Initialization
//==============================================================================

static struct netif slip_netif;

static void network_init(void)
{
    ip4_addr_t ipaddr, netmask, gw;

    printf("\r\n");
    printf("=========================================\r\n");
    printf("lwIP TCP/IP Stack - SLIP Demo\r\n");
    printf("=========================================\r\n");
    printf("Version: %s\r\n", LWIP_VERSION_STRING);
    printf("\r\n");

    /* Initialize lwIP */
    printf("Initializing lwIP...\r\n");
    lwip_init();
    printf("  OK\r\n");

    /* Set up IP addresses */
    ip4addr_aton(DEVICE_IP, &ipaddr);
    ip4addr_aton(NETMASK, &netmask);
    ip4addr_aton(GATEWAY_IP, &gw);

    printf("Network Configuration:\r\n");
    printf("  IP address: %s\r\n", DEVICE_IP);
    printf("  Netmask:    %s\r\n", NETMASK);
    printf("  Gateway:    %s\r\n", GATEWAY_IP);
    printf("\r\n");

    /* Add SLIP interface */
    printf("Adding SLIP interface...\r\n");
    netif_add(&slip_netif, &ipaddr, &netmask, &gw, NULL, slipif_init, ip_input);

    if (slip_netif.output == NULL) {
        printf("ERROR: SLIP interface initialization failed!\r\n");
        while(1);
    }

    /* Set as default interface */
    netif_set_default(&slip_netif);

    /* Bring interface up */
    netif_set_up(&slip_netif);
    netif_set_link_up(&slip_netif);

    printf("  OK - SLIP ready\r\n");
    printf("\r\n");

    /* Start echo server */
    printf("Starting TCP echo server...\r\n");
    echo_server_init();
    printf("\r\n");

    printf("=========================================\r\n");
    printf("Ready! Waiting for connections...\r\n");
    printf("=========================================\r\n");
    printf("\r\n");
    printf("On Linux host, run:\r\n");
    printf("  sudo tools/slattach_1m/slattach_1m -p slip -s 1000000 -L /dev/ttyUSB0 &\r\n");
    printf("  sudo ifconfig sl0 192.168.100.1 pointopoint 192.168.100.2 up\r\n");
    printf("  ping 192.168.100.2\r\n");
    printf("  telnet 192.168.100.2 7777\r\n");
    printf("\r\n");
}

//==============================================================================
// Statistics - Removed (would interfere with SLIP on UART)
//==============================================================================
// Note: All runtime printing is disabled because UART is 100% dedicated to
// SLIP protocol after initialization. Startup messages are OK before SLIP starts.

//==============================================================================
// Main Loop
//==============================================================================

int main(void)
{
    /* Banner */
    printf("\r\n");
    printf("==========================================\r\n");
    printf("PicoRV32 SLIP + lwIP TCP/IP Demo\r\n");
    printf("==========================================\r\n");
    printf("\r\n");

    /* Initialize networking */
    network_init();

    /* Main loop */
    while (1) {
        /* Poll SLIP interface for incoming packets */
        slipif_poll(&slip_netif);

        /* Process lwIP timers (TCP retransmission, ARP, etc.) */
        sys_check_timeouts();
    }

    return 0;
}
