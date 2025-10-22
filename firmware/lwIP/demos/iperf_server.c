/*
 * lwIP iperf Server for PicoRV32
 *
 * Uses lwIP's built-in iperf server for performance testing over SLIP.
 * Much simpler and more reliable than custom protocols!
 *
 * Test with: iperf -c 192.168.100.2 -t 10
 *
 * Copyright (c) October 2025 Michael Wolak
 */

#include <stdio.h>
#include <stdlib.h>
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/timeouts.h"
#include "lwip/ip.h"
#include "netif/slipif.h"
#include "lwip/ip_addr.h"
#include "lwip/apps/lwiperf.h"

//==============================================================================
// Configuration
//==============================================================================

#define DEVICE_IP      "192.168.100.2"
#define NETMASK        "255.255.255.0"
#define GATEWAY_IP     "192.168.100.1"

//==============================================================================
// iperf Report Callback
//==============================================================================

static void lwiperf_report(void *arg, enum lwiperf_report_type report_type,
                          const ip_addr_t* local_addr, u16_t local_port,
                          const ip_addr_t* remote_addr, u16_t remote_port,
                          u32_t bytes_transferred, u32_t ms_duration,
                          u32_t bandwidth_kbitpsec)
{
    (void)arg;
    (void)report_type;
    (void)local_addr;
    (void)local_port;
    (void)remote_addr;
    (void)remote_port;
    (void)bytes_transferred;
    (void)ms_duration;
    (void)bandwidth_kbitpsec;

    /* NO printf - corrupts SLIP! */
    /* Results will be shown by iperf client */
}

//==============================================================================
// Network Initialization
//==============================================================================

static struct netif slip_netif;

static void network_init(void) {
    ip4_addr_t ipaddr, netmask, gw;

    /* NO printf - corrupts SLIP once started! */

    lwip_init();

    ip4addr_aton(DEVICE_IP, &ipaddr);
    ip4addr_aton(NETMASK, &netmask);
    ip4addr_aton(GATEWAY_IP, &gw);

    netif_add(&slip_netif, &ipaddr, &netmask, &gw, NULL, slipif_init, ip_input);
    netif_set_default(&slip_netif);
    netif_set_up(&slip_netif);

    /* Start iperf server on default port 5001 */
    lwiperf_start_tcp_server_default(lwiperf_report, NULL);

    /* Server is ready - test with: iperf -c 192.168.100.2 -t 10 */
}

//==============================================================================
// Main Loop
//==============================================================================

int main(void) {
    network_init();

    /* NO printf - corrupts SLIP! */

    /* Main loop: process lwIP timeouts and SLIP input */
    while (1) {
        sys_check_timeouts();
        slipif_poll(&slip_netif);
    }

    return 0;
}
