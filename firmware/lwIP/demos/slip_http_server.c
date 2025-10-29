//===============================================================================
// HTTP Server over SLIP - lwIP Demo for PicoRV32
//
// Demonstrates basic HTTP web server over serial line (SLIP protocol)
//
// Features:
// - lwIP stack in NO_SYS mode (bare metal, no RTOS)
// - SLIP interface over UART (1000000 baud / 1 Mbaud)
// - Basic HTTP server on port 80 (lwIP httpd app)
// - ICMP (ping) support
//
// Linux Host Setup:
//   sudo tools/slattach_1m/slattach_1m -p slip -s 1000000 -L /dev/ttyUSB0 &
//   sudo ifconfig sl0 192.168.100.1 pointopoint 192.168.100.2 up
//   ping 192.168.100.2
//
// Browse to: http://192.168.100.2/
//
// Note: This is a simplified version without CGI/SSI support. Those features
//       require additional lwipopts.h configuration (LWIP_HTTPD_CGI, etc.)
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
#include "lwip/ip.h"
#include "lwip/stats.h"

/* lwIP SLIP interface */
#include "netif/slipif.h"

/* lwIP HTTP server */
#include "lwip/apps/httpd.h"

//==============================================================================
// Configuration
//==============================================================================

#define DEVICE_IP       "192.168.100.2"    /* This device (PicoRV32) */
#define GATEWAY_IP      "192.168.100.1"    /* Linux host */
#define NETMASK         "255.255.255.0"

//==============================================================================
// LED Control (for activity indication)
//==============================================================================

#define LED_CONTROL (*(volatile uint32_t*)0x80000010)

static void led_set(uint8_t state) {
    LED_CONTROL = state;
}

//==============================================================================
// Timer Interrupt Handler (for lwIP timing)
//
// Pattern copied from slip_echo_server.c - proven working implementation
// Timer fires every 1ms, increments lwIP millisecond counter
//==============================================================================

#define TIMER_BASE      0x80000020
#define TIMER_SR        (*(volatile uint32_t*)(TIMER_BASE + 0x04))
#define TIMER_SR_UIF    (1 << 0)

/* Extern function in sys_arch.c - increments ms_count */
extern void sys_timer_tick(void);

/*
 * IRQ Handler - Called by start.S when interrupt occurs
 *
 * Exactly matches slip_echo_server.c pattern:
 * 1. Check if Timer IRQ[0] fired
 * 2. Clear interrupt flag (CRITICAL - must do this!)
 * 3. Update counter variable
 * 4. Return (retirq in start.S handles the rest)
 */
void irq_handler(uint32_t irqs) {
    /* Check if Timer interrupt (IRQ[0]) */
    if (irqs & (1 << 0)) {
        /* CRITICAL: Clear the interrupt source FIRST */
        /* Write 1 to UIF bit to clear it - same as slip_echo_server.c */
        TIMER_SR = TIMER_SR_UIF;

        /* Increment lwIP millisecond counter */
        /* This is like slip_echo_server.c incrementing ms_count */
        sys_timer_tick();
    }
}

//==============================================================================
// Network Interface Setup
//==============================================================================

static struct netif slip_netif;

//==============================================================================
// Main Function
//==============================================================================

int main(void)
{
    ip4_addr_t ipaddr, netmask, gw;

    printf("\r\n========================================\r\n");
    printf("lwIP HTTP Server over SLIP\r\n");
    printf("========================================\r\n");
    printf("PicoRV32 FPGA - Olimex iCE40HX8K\r\n");
    printf("lwIP version: %u.%u.%u\r\n\r\n",
           LWIP_VERSION_MAJOR, LWIP_VERSION_MINOR, LWIP_VERSION_REVISION);

    printf("Configuration:\r\n");
    printf("  TCP_MSS:     %d bytes\r\n", TCP_MSS);
    printf("  TCP_WND:     %d bytes\r\n", TCP_WND);
    printf("  TCP_SND_BUF: %d bytes\r\n", TCP_SND_BUF);
    printf("  PBUF_POOL:   %d x %d = %d KB\r\n",
           PBUF_POOL_SIZE, PBUF_POOL_BUFSIZE,
           (PBUF_POOL_SIZE * PBUF_POOL_BUFSIZE) / 1024);
    printf("\r\n");

    /* Initialize lwIP stack */
    printf("Initializing lwIP stack...\r\n");
    lwip_init();

    /* Configure IP addresses */
    ip4addr_aton(DEVICE_IP, &ipaddr);
    ip4addr_aton(GATEWAY_IP, &gw);
    ip4addr_aton(NETMASK, &netmask);

    /* Add SLIP network interface */
    printf("Adding SLIP interface...\r\n");
    netif_add(&slip_netif, &ipaddr, &netmask, &gw, NULL, slipif_init, ip_input);

    /* Set as default interface and bring up */
    netif_set_default(&slip_netif);
    netif_set_up(&slip_netif);
    netif_set_link_up(&slip_netif);

    printf("SLIP interface configured:\r\n");
    printf("  IP:      %s\r\n", DEVICE_IP);
    printf("  Gateway: %s\r\n", GATEWAY_IP);
    printf("  Netmask: %s\r\n", NETMASK);

    /* Initialize HTTP server (basic static pages only) */
    printf("Starting HTTP server...\r\n");
    httpd_init();

    printf("\r\n========================================\r\n");
    printf("HTTP Server Ready!\r\n");
    printf("========================================\r\n");
    printf("Browse to: http://192.168.100.2/\r\n");
    printf("(Serves static content from makefsdata)\r\n");
    printf("\r\n");

    //==========================================================================
    // Timer Interrupt Setup (Required for lwIP timeouts)
    // CRITICAL: Must be done BEFORE main loop starts!
    // After this point, UART belongs to SLIP - NO MORE PRINTF!
    //==========================================================================
    extern void sys_init_timing(void);
    sys_init_timing();

    //==========================================================================
    // SLIP Protocol Active - UART Lockout!
    //==========================================================================
    // From this point forward, UART is 100% dedicated to SLIP protocol.
    // Any printf() or UART writes will corrupt SLIP packets and break TCP/IP.
    // Timer interrupts are now active and lwIP is processing packets.
    //==========================================================================

    /* Main loop - poll SLIP and process lwIP timers */
    while (1) {
        /* Poll SLIP interface for incoming packets */
        slipif_poll(&slip_netif);

        /* Process lwIP timers (TCP retransmit, ARP, etc.) */
        sys_check_timeouts();
    }

    return 0;
}
