//===============================================================================
// HTTP Server over SLIP - lwIP Demo for PicoRV32
//
// Demonstrates HTTP web server over serial line (SLIP protocol)
// Based on Espressif ESP-IDF lwIP configuration patterns
//
// Features:
// - lwIP stack in NO_SYS mode (bare metal, no RTOS)
// - SLIP interface over UART (1000000 baud / 1 Mbaud)
// - HTTP server on port 80 (lwIP httpd app)
// - SSI (Server-Side Includes) for dynamic content
// - CGI (Common Gateway Interface) for LED control
// - ICMP (ping) support
//
// Linux Host Setup:
//   sudo tools/slattach_1m/slattach_1m -p slip -s 1000000 -L /dev/ttyUSB0 &
//   sudo ifconfig sl0 192.168.100.1 pointopoint 192.168.100.2 up
//   ping 192.168.100.2
//
// Browse to: http://192.168.100.2/
//
// Copyright (c) October 2025 Michael Wolak
// Email: mikewolak@gmail.com, mike@epromfoundry.com
//===============================================================================

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

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
// Hardware Peripherals
//==============================================================================

#define LED_CONTROL (*(volatile uint32_t*)0x80000010)
#define TIMER_COUNTER (*(volatile uint32_t*)0x80000028)

static void led_set(uint8_t state) {
    LED_CONTROL = state;
}

static uint8_t led_state = 0;

//==============================================================================
// SSI (Server-Side Includes) Handler
//
// Called when httpd encounters tags like <!--#uptime--> in HTML files
//==============================================================================

static const char *ssi_tags[] = {
    "uptime",      /* <!--#uptime--> - System uptime */
    "timer",       /* <!--#timer-->  - Timer counter */
    "led",         /* <!--#led-->    - LED state */
    "lwip_ver"     /* <!--#lwip_ver--> - lwIP version */
};

static u16_t ssi_handler(int iIndex, char *pcInsert, int iInsertLen)
{
    static uint32_t uptime_seconds = 0;

    switch (iIndex) {
        case 0:  /* uptime */
            uptime_seconds = TIMER_COUNTER / 50000000;  /* 50 MHz clock */
            snprintf(pcInsert, iInsertLen, "%lu seconds", uptime_seconds);
            break;

        case 1:  /* timer */
            snprintf(pcInsert, iInsertLen, "%lu", TIMER_COUNTER);
            break;

        case 2:  /* led */
            snprintf(pcInsert, iInsertLen, "%s", led_state ? "ON" : "OFF");
            break;

        case 3:  /* lwip_ver */
            snprintf(pcInsert, iInsertLen, "%u.%u.%u",
                    LWIP_VERSION_MAJOR, LWIP_VERSION_MINOR, LWIP_VERSION_REVISION);
            break;

        default:
            snprintf(pcInsert, iInsertLen, "N/A");
            break;
    }

    return strlen(pcInsert);
}

//==============================================================================
// CGI (Common Gateway Interface) Handler
//
// Called when httpd receives requests like /led.cgi?state=on
//==============================================================================

static const char *cgi_led_handler(int iIndex, int iNumParams, char *pcParam[], char *pcValue[])
{
    (void)iIndex;  /* Unused parameter */

    /* Parse parameters */
    for (int i = 0; i < iNumParams; i++) {
        if (strcmp(pcParam[i], "state") == 0) {
            if (strcmp(pcValue[i], "on") == 0) {
                led_state = 1;
                led_set(1);
            } else if (strcmp(pcValue[i], "off") == 0) {
                led_state = 0;
                led_set(0);
            } else if (strcmp(pcValue[i], "toggle") == 0) {
                led_state = !led_state;
                led_set(led_state);
            }
        }
    }

    /* Return page to display */
    return "/index.shtml";
}

static const tCGI cgi_handlers[] = {
    { "/led.cgi", cgi_led_handler }
};

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

    printf("Configuration (based on Espressif ESP-IDF patterns):\r\n");
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

    printf("SLIP interface configured:\r\n");
    printf("  IP:      %s\r\n", DEVICE_IP);
    printf("  Gateway: %s\r\n", GATEWAY_IP);
    printf("  Netmask: %s\r\n", NETMASK);

    /* Initialize HTTP server */
    printf("Starting HTTP server...\r\n");
    httpd_init();

    /* Register SSI handler */
    http_set_ssi_handler(ssi_handler, ssi_tags, sizeof(ssi_tags) / sizeof(ssi_tags[0]));

    /* Register CGI handlers */
    http_set_cgi_handlers(cgi_handlers, sizeof(cgi_handlers) / sizeof(cgi_handlers[0]));

    printf("\r\n========================================\r\n");
    printf("HTTP Server Ready!\r\n");
    printf("========================================\r\n");
    printf("Browse to: http://192.168.100.2/\r\n");
    printf("\r\n");

    //==========================================================================
    // Main Loop - NO PRINTF() ALLOWED AFTER THIS POINT!
    //==========================================================================
    // Note: UART is 100% dedicated to SLIP protocol after initialization.
    // Any printf() in the main loop will corrupt SLIP packets and break TCP/IP.
    // Use LED for runtime status indication only.
    //==========================================================================

    /* LED heartbeat */
    uint32_t last_blink = 0;
    uint8_t blink_state = 0;

    /* Main loop - poll SLIP and process lwIP timers */
    while (1) {
        /* Poll SLIP interface for incoming packets */
        slipif_poll(&slip_netif);

        /* Process lwIP timers (TCP retransmit, ARP, etc.) */
        sys_check_timeouts();

        /* Heartbeat LED (blink every ~500ms) */
        uint32_t now = TIMER_COUNTER;
        if ((now - last_blink) > 25000000) {  /* 50MHz / 2 = 500ms */
            last_blink = now;
            blink_state = !blink_state;
            if (!led_state) {  /* Only blink if LED not manually controlled */
                led_set(blink_state);
            }
        }
    }

    return 0;
}
