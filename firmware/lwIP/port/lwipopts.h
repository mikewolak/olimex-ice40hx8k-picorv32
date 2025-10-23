/*
 * lwIP Configuration for PicoRV32 - Bare Metal with SLIP
 *
 * NO_SYS = 1 (bare metal, no OS)
 * Uses SLIP interface over UART
 *
 * Copyright (c) October 2025 Michael Wolak
 */

#ifndef LWIP_LWIPOPTS_H
#define LWIP_LWIPOPTS_H

/*
 * NO_SYS==1: Bare metal (no OS/RTOS)
 * Must call sys_check_timeouts() periodically from main loop
 */
#define NO_SYS                  1
#define LWIP_TIMERS             1

/*
 * Memory Configuration
 * Based on Espressif ESP-IDF lwIP configuration, adapted for SLIP
 */
#define MEM_ALIGNMENT           4
#define MEM_SIZE                (16*1024)   /* 16KB heap for lwIP */

#define MEMP_NUM_PBUF           16          /* Protocol buffer descriptors */
#define MEMP_NUM_UDP_PCB        4           /* UDP connections */
#define MEMP_NUM_TCP_PCB        16          /* TCP connections (ESP-IDF: MAX_ACTIVE_TCP=16) */
#define MEMP_NUM_TCP_PCB_LISTEN 16          /* TCP listen sockets (ESP-IDF: MAX_LISTENING_TCP=16) */
#define MEMP_NUM_TCP_SEG        20          /* TCP segments (>= TCP_SND_QUEUELEN) */
#define MEMP_NUM_NETCONN        0           /* Not using netconn API */

#define PBUF_POOL_SIZE          16          /* Packet buffer pool (16 * 768 = 12KB total) */
#define PBUF_POOL_BUFSIZE       768         /* 768 bytes per pbuf - fits TCP_MSS(536) + headers(40) + margin */

/*
 * Protocol Features
 */
#define LWIP_ARP                0           /* No ARP (SLIP is point-to-point) */
#define LWIP_ETHERNET           0           /* No Ethernet (using SLIP) */
#define LWIP_IPV4               1           /* IPv4 support */
#define LWIP_IPV6               0           /* Disable IPv6 to save memory */
#define LWIP_ICMP               1           /* ICMP (ping) support */
#define LWIP_RAW                0           /* Disable raw API */
#define LWIP_UDP                1           /* UDP support */
#define LWIP_TCP                1           /* TCP support */

/*
 * TCP Configuration
 * Based on Espressif ESP-IDF defaults (4x MSS pattern), adapted for SLIP
 * ESP-IDF: TCP_MSS=1440, TCP_WND=5760 (4*MSS), TCP_SND_BUF=5760 (4*MSS)
 * SLIP: TCP_MSS=536 (safe for low MTU), same 4x pattern
 */
#define TCP_MSS                 536         /* Max segment size (536 = standard for SLIP/low MTU) */
#define TCP_WND                 (4*TCP_MSS) /* TCP window: 2144 bytes (ESP-IDF pattern: 4*MSS) */
#define TCP_SND_BUF             (4*TCP_MSS) /* TCP send buffer: 2144 bytes (ESP-IDF pattern: 4*MSS) */
#define TCP_SND_QUEUELEN        ((4 * (TCP_SND_BUF) + (TCP_MSS - 1)) / (TCP_MSS))
#define TCP_LISTEN_BACKLOG      1           /* Enable listen backlog */
#define LWIP_TCP_KEEPALIVE      1           /* Enable TCP keepalive */

/*
 * DHCP/DNS
 */
#define LWIP_DHCP               0           /* No DHCP (static IP for SLIP) */
#define LWIP_DNS                0           /* No DNS to save memory */

/*
 * SLIP Configuration
 */
#define LWIP_HAVE_SLIPIF        1           /* Enable SLIP interface */
#define SLIP_USE_RX_THREAD      0           /* NO_SYS, so no threads */
#define SLIP_RX_FROM_ISR        0           /* Polling mode */

/*
 * HTTP Server Configuration (lwIP httpd app)
 */
#define LWIP_HTTPD              1           /* Enable HTTP server */
#define LWIP_HTTPD_CGI          1           /* Enable CGI support */
#define LWIP_HTTPD_SSI          1           /* Enable SSI support */
#define LWIP_HTTPD_SUPPORT_POST 0           /* Disable POST for simplicity */
#define LWIP_HTTPD_DYNAMIC_HEADERS 1        /* Allow dynamic headers */
#define LWIP_HTTPD_CUSTOM_FILES 0           /* Use makefsdata filesystem */
#define LWIP_HTTPD_MAX_TAG_NAME_LEN  16     /* SSI tag name length */
#define LWIP_HTTPD_MAX_TAG_INSERT_LEN 256   /* SSI insert buffer */

/*
 * APIs
 */
#define LWIP_NETCONN            0           /* Disable netconn API (not for NO_SYS) */
#define LWIP_SOCKET             0           /* Disable BSD socket API (not for NO_SYS) */

/*
 * Statistics
 */
#define LWIP_STATS              1           /* Enable statistics */
#define LWIP_STATS_DISPLAY      1           /* Allow stats display */

/*
 * Debugging
 * MUST BE OFF - debug output corrupts SLIP!
 */
#define LWIP_DEBUG              0
#define LWIP_DBG_MIN_LEVEL      LWIP_DBG_LEVEL_OFF
#define LWIP_DBG_TYPES_ON       LWIP_DBG_OFF

#define ETHARP_DEBUG            LWIP_DBG_OFF
#define NETIF_DEBUG             LWIP_DBG_OFF
#define PBUF_DEBUG              LWIP_DBG_OFF
#define API_LIB_DEBUG           LWIP_DBG_OFF
#define API_MSG_DEBUG           LWIP_DBG_OFF
#define SOCKETS_DEBUG           LWIP_DBG_OFF
#define ICMP_DEBUG              LWIP_DBG_OFF
#define INET_DEBUG              LWIP_DBG_OFF
#define IP_DEBUG                LWIP_DBG_OFF
#define IP_REASS_DEBUG          LWIP_DBG_OFF
#define RAW_DEBUG               LWIP_DBG_OFF
#define MEM_DEBUG               LWIP_DBG_OFF
#define MEMP_DEBUG              LWIP_DBG_OFF
#define SYS_DEBUG               LWIP_DBG_OFF
#define TCP_DEBUG               LWIP_DBG_OFF
#define TCP_INPUT_DEBUG         LWIP_DBG_OFF
#define TCP_OUTPUT_DEBUG        LWIP_DBG_OFF
#define TCP_RTO_DEBUG           LWIP_DBG_OFF
#define TCP_CWND_DEBUG          LWIP_DBG_OFF
#define TCP_WND_DEBUG           LWIP_DBG_OFF
#define TCP_FR_DEBUG            LWIP_DBG_OFF
#define TCP_QLEN_DEBUG          LWIP_DBG_OFF
#define TCP_RST_DEBUG           LWIP_DBG_OFF
#define UDP_DEBUG               LWIP_DBG_OFF
#define TCPIP_DEBUG             LWIP_DBG_OFF
#define SLIP_DEBUG              LWIP_DBG_OFF
#define DHCP_DEBUG              LWIP_DBG_OFF

/*
 * Checksum Configuration
 * Let lwIP compute checksums in software (no hardware offload)
 */
#define CHECKSUM_GEN_IP         1
#define CHECKSUM_GEN_UDP        1
#define CHECKSUM_GEN_TCP        1
#define CHECKSUM_GEN_ICMP       1
#define CHECKSUM_CHECK_IP       1
#define CHECKSUM_CHECK_UDP      1
#define CHECKSUM_CHECK_TCP      1
#define CHECKSUM_CHECK_ICMP     1

/*
 * Minimal assert - just hang
 */
#define LWIP_PLATFORM_ASSERT(x) do { \
    printf("LWIP ASSERT: %s\n", x); \
    while(1); \
} while(0)

/*
 * Debug output to UART via printf
 */
extern int printf(const char *fmt, ...);
#define LWIP_PLATFORM_DIAG(x) do { printf x; } while(0)

#endif /* LWIP_LWIPOPTS_H */
