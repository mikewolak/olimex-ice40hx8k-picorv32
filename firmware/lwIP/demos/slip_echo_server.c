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
    uint8_t banner_sent;        /* Flag: has welcome banner been sent? */
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

    /* Send welcome banner on first receive if not sent yet */
    /* Send in small chunks due to PBUF_POOL_BUFSIZE = 256 */
    if (!es->banner_sent) {
        const char *banner[] = {
            "\r\n",
            "================================================================================\r\n",
            "                         PICORV32 FPGA ECHO SERVER                             \r\n",
            "================================================================================\r\n",
            "\r\n",
            "  Platform: Olimex iCE40HX8K-EVB FPGA Board                                    \r\n",
            "  CPU:      PicoRV32 RISC-V RV32IM @ 50 MHz                                    \r\n",
            "  Memory:   512 KB SRAM                                                        \r\n",
            "  Network:  lwIP TCP/IP Stack v2.2.0 (NO_SYS mode)                             \r\n",
            "  Link:     SLIP over UART @ 1 Mbaud (~90-100 KB/sec)                          \r\n",
            "\r\n",
            "================================================================================\r\n",
            "  TCP ECHO SERVER - Port 7777                                                  \r\n",
            "================================================================================\r\n",
            "\r\n",
            "  Everything you type will be echoed back to you.                              \r\n",
            "  Perfect for testing TCP/IP connectivity and throughput!                      \r\n",
            "\r\n",
            "  Tips:                                                                         \r\n",
            "    - Try pasting large text blocks to test buffer handling                    \r\n",
            "    - Each pbuf is 256 bytes, chains handle larger messages                    \r\n",
            "    - Watch the LED blink with activity                                        \r\n",
            "    - Press Ctrl+] then 'quit' to exit telnet                                  \r\n",
            "\r\n",
            "  Author: Michael Wolak (mikewolak@gmail.com)                                  \r\n",
            "  Date:   October 2025                                                         \r\n",
            "\r\n",
            "================================================================================\r\n",
            "  Ready to echo! Type something...                                             \r\n",
            "================================================================================\r\n",
            "\r\n"
        };

        /* Send banner line by line to avoid buffer issues */
        err_t banner_err = ERR_OK;
        int lines_sent = 0;
        for (int i = 0; i < sizeof(banner)/sizeof(banner[0]) && banner_err == ERR_OK; i++) {
            banner_err = tcp_write(tpcb, banner[i], strlen(banner[i]), TCP_WRITE_FLAG_COPY);
            if (banner_err == ERR_OK) {
                es->bytes_sent += strlen(banner[i]);
                lines_sent++;
            } else {
                printf("Banner line %d failed: err=%d, len=%u\r\n", i, banner_err, strlen(banner[i]));
                break;
            }
        }

        if (banner_err == ERR_OK) {
            es->banner_sent = 1;  /* Mark banner as sent */
            tcp_output(tpcb);      /* Flush banner immediately */
            printf("Banner sent: %d lines\r\n", lines_sent);
        } else {
            printf("Banner incomplete: %d/%d lines (err=%d)\r\n",
                   lines_sent, (int)(sizeof(banner)/sizeof(banner[0])), banner_err);
        }
    }

    /* Update received byte count */
    es->bytes_received += p->tot_len;

    /* Prepare response header */
    char header[80];
    int header_len = snprintf(header, sizeof(header),
                              "\r\n[Message Received (%u Bytes) - Relaying...]\r\n",
                              p->tot_len);

    /* Send header first */
    ret_err = tcp_write(tpcb, header, header_len, TCP_WRITE_FLAG_COPY);
    if (ret_err == ERR_OK) {
        es->bytes_sent += header_len;
    }

    /* Echo data back - handle pbuf chain (may be >256 bytes) */
    struct pbuf *q;
    for (q = p; q != NULL && ret_err == ERR_OK; q = q->next) {
        ret_err = tcp_write(tpcb, q->payload, q->len, TCP_WRITE_FLAG_COPY);
        if (ret_err == ERR_OK) {
            es->bytes_sent += q->len;
        }
    }

    /* Add footer */
    const char *footer = "\r\n[End of Echo]\r\n\r\n";
    if (ret_err == ERR_OK) {
        ret_err = tcp_write(tpcb, footer, strlen(footer), TCP_WRITE_FLAG_COPY);
        if (ret_err == ERR_OK) {
            es->bytes_sent += strlen(footer);
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

    /* Send echo data and ACK with updated window */
    tcp_output(tpcb);

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
    es->banner_sent = 0;        /* Banner not sent yet */

    /* Set up TCP callbacks */
    tcp_arg(newpcb, es);
    tcp_recv(newpcb, echo_recv);
    tcp_err(newpcb, echo_err);

    /* Don't send banner here - defer to first recv callback */
    /* This avoids buffer issues in accept callback */

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
