/*
 * Simple TCP Performance Server for PicoRV32
 *
 * Minimal TCP server optimized for performance testing over SLIP.
 * Based on echo_server pattern that we know works.
 *
 * Test with: iperf -c 192.168.100.2 -p 5001 -t 10
 *
 * Copyright (c) October 2025 Michael Wolak
 */

#include <stdio.h>
#include <string.h>
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/timeouts.h"
#include "lwip/ip.h"
#include "lwip/tcp.h"
#include "netif/slipif.h"
#include "lwip/ip_addr.h"

//==============================================================================
// Configuration
//==============================================================================

#define DEVICE_IP      "192.168.100.2"
#define NETMASK        "255.255.255.0"
#define GATEWAY_IP     "192.168.100.1"
#define PERF_PORT      5001

//==============================================================================
// TCP Performance Server
//==============================================================================

static err_t perf_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    (void)arg;
    (void)err;

    if (p == NULL) {
        /* Connection closed */
        tcp_close(tpcb);
        return ERR_OK;
    }

    /* Receive data - just consume it for performance testing */
    tcp_recved(tpcb, p->tot_len);

    /* Force immediate ACK to keep window open (critical for NO_SYS mode) */
    tcp_output(tpcb);

    /* Free pbuf LAST - lwIP may need it during tcp_output() */
    pbuf_free(p);

    return ERR_OK;
}

static err_t perf_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    (void)arg;
    (void)err;

    /* Set up receive callback */
    tcp_recv(newpcb, perf_recv);

    return ERR_OK;
}

static void perf_server_init(void)
{
    struct tcp_pcb *pcb;

    pcb = tcp_new();
    if (pcb == NULL) {
        /* Cannot create PCB - hang */
        while(1) { }
    }

    err_t err = tcp_bind(pcb, IP_ADDR_ANY, PERF_PORT);
    if (err != ERR_OK) {
        /* Cannot bind - hang */
        while(1) { }
    }

    pcb = tcp_listen(pcb);
    if (pcb == NULL) {
        /* Cannot listen - hang */
        while(1) { }
    }

    tcp_accept(pcb, perf_accept);
}

//==============================================================================
// Network Initialization
//==============================================================================

static struct netif slip_netif;

static void network_init(void) {
    ip4_addr_t ipaddr, netmask, gw;

    lwip_init();

    ip4addr_aton(DEVICE_IP, &ipaddr);
    ip4addr_aton(NETMASK, &netmask);
    ip4addr_aton(GATEWAY_IP, &gw);

    netif_add(&slip_netif, &ipaddr, &netmask, &gw, NULL, slipif_init, ip_input);
    netif_set_default(&slip_netif);
    netif_set_up(&slip_netif);

    /* Start performance server on port 5001 */
    perf_server_init();
}

//==============================================================================
// Main Loop
//==============================================================================

int main(void) {
    network_init();

    /* Main loop: process lwIP timeouts and SLIP input */
    while (1) {
        sys_check_timeouts();
        slipif_poll(&slip_netif);
    }

    return 0;
}
