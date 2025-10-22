//===============================================================================
// SLIP Performance Test Server - lwIP TCP/IP Performance Benchmark
//
// High-performance bidirectional TCP test server with CRC32 validation
//
// Features:
// - 32KB buffer transfers (conservative for current memory layout)
// - Unidirectional and bidirectional modes
// - CRC32 validation (0xEDB88320 polynomial)
// - Timeout handling
// - Real-time statistics
//
// Protocol:
//   Port 8888
//   Message format: [Type:4][Length:4][Payload:N]
//
// Linux Client Setup:
//   sudo tools/slattach_1m/slattach_1m -p slip -s 1000000 -L /dev/ttyUSB0
//   sudo ifconfig sl0 192.168.100.1 pointopoint 192.168.100.2 up
//   tools/slip_perf_client/slip_perf_client 192.168.100.2
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
#include "lwip/stats.h"
#include "netif/slipif.h"
#include "lwip/tcp.h"

//==============================================================================
// Configuration
//==============================================================================

#define DEVICE_IP       "192.168.100.2"
#define GATEWAY_IP      "192.168.100.1"
#define NETMASK         "255.255.255.0"

#define PERF_PORT       8888
#define MAX_BUFFER_SIZE (32 * 1024)   // 32KB - conservative for current memory layout

//==============================================================================
// Protocol Message Types
//==============================================================================

#define MSG_CAPS_REQ    0x01
#define MSG_CAPS_RESP   0x02
#define MSG_TEST_START  0x03
#define MSG_TEST_ACK    0x04
#define MSG_DATA_CRC    0x05
#define MSG_DATA_BLOCK  0x06
#define MSG_DATA_ACK    0x07
#define MSG_TEST_STOP   0x08
#define MSG_ERROR       0xFF

//==============================================================================
// CRC32 Implementation (matches firmware/hexedit.c polynomial 0xEDB88320)
//==============================================================================

static uint32_t crc32_table[256];
static int crc32_initialized = 0;

static void crc32_init(void) {
    if (crc32_initialized) return;
    for (int i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320 : 0);
        }
        crc32_table[i] = crc;
    }
    crc32_initialized = 1;
}

static uint32_t calculate_crc32(const uint8_t *data, uint32_t length) {
    crc32_init();
    uint32_t crc = 0xFFFFFFFF;
    for (uint32_t i = 0; i < length; i++) {
        crc = (crc >> 8) ^ crc32_table[(crc ^ data[i]) & 0xFF];
    }
    return ~crc;
}

//==============================================================================
// Performance Test State
//==============================================================================

/* Global test buffer - allocated once at startup */
static uint8_t *g_test_buffer = NULL;
static uint32_t g_buffer_size = 0;

struct perf_state {
    struct tcp_pcb *pcb;
    uint32_t block_size;
    uint32_t bytes_rx;
    uint32_t bytes_tx;
    uint32_t packets_rx;
    uint32_t packets_tx;
    uint32_t errors;
    uint8_t test_active;
    uint32_t expected_crc;

    /* Simple buffering for incomplete messages */
    struct pbuf *pending;  /* Accumulated pbuf chain */
};

//==============================================================================
// Helper Functions
//==============================================================================

static void send_message(struct tcp_pcb *tpcb, uint32_t type, const void *payload, uint32_t length) {
    uint8_t header[8];

    /* Build header: [Type:4][Length:4] */
    header[0] = (type >> 24) & 0xFF;
    header[1] = (type >> 16) & 0xFF;
    header[2] = (type >> 8) & 0xFF;
    header[3] = type & 0xFF;

    header[4] = (length >> 24) & 0xFF;
    header[5] = (length >> 16) & 0xFF;
    header[6] = (length >> 8) & 0xFF;
    header[7] = length & 0xFF;

    /* Send header */
    tcp_write(tpcb, header, 8, TCP_WRITE_FLAG_COPY);

    /* Send payload if present */
    if (payload && length > 0) {
        tcp_write(tpcb, payload, length, TCP_WRITE_FLAG_COPY);
    }

    tcp_output(tpcb);
}

//==============================================================================
// Protocol Handlers
//==============================================================================

static void handle_caps_req(struct perf_state *ps) {
    uint8_t payload[4];

    /* Send actual allocated buffer size */
    payload[0] = (g_buffer_size >> 24) & 0xFF;
    payload[1] = (g_buffer_size >> 16) & 0xFF;
    payload[2] = (g_buffer_size >> 8) & 0xFF;
    payload[3] = g_buffer_size & 0xFF;

    send_message(ps->pcb, MSG_CAPS_RESP, payload, 4);
}

static void handle_test_start(struct perf_state *ps, const uint8_t *payload) {
    uint32_t block_size;
    uint8_t error_payload[12];

    /* Extract block size from payload */
    block_size = ((uint32_t)payload[0] << 24) |
                 ((uint32_t)payload[1] << 16) |
                 ((uint32_t)payload[2] << 8) |
                 ((uint32_t)payload[3]);

    if (block_size > g_buffer_size) {
        /* Send error with reason code 1 = size too large, plus actual values */
        error_payload[0] = 0;
        error_payload[1] = 0;
        error_payload[2] = 0;
        error_payload[3] = 1;  /* Error code: 1 = size exceeds max */

        /* Include requested block_size */
        error_payload[4] = (block_size >> 24) & 0xFF;
        error_payload[5] = (block_size >> 16) & 0xFF;
        error_payload[6] = (block_size >> 8) & 0xFF;
        error_payload[7] = block_size & 0xFF;

        /* Include actual g_buffer_size */
        error_payload[8] = (g_buffer_size >> 24) & 0xFF;
        error_payload[9] = (g_buffer_size >> 16) & 0xFF;
        error_payload[10] = (g_buffer_size >> 8) & 0xFF;
        error_payload[11] = g_buffer_size & 0xFF;

        send_message(ps->pcb, MSG_ERROR, error_payload, 12);
        return;
    }

    /* Use pre-allocated global buffer */
    if (!g_test_buffer) {
        /* Send error with reason code 2 = buffer not allocated */
        error_payload[0] = 0;
        error_payload[1] = 0;
        error_payload[2] = 0;
        error_payload[3] = 2;  /* Error code: 2 = buffer not available */
        send_message(ps->pcb, MSG_ERROR, error_payload, 4);
        return;
    }

    ps->block_size = block_size;
    ps->test_active = 1;
    ps->bytes_rx = 0;
    ps->bytes_tx = 0;
    ps->packets_rx = 0;
    ps->packets_tx = 0;
    ps->errors = 0;

    send_message(ps->pcb, MSG_TEST_ACK, NULL, 0);
}

static void handle_data_crc(struct perf_state *ps, const uint8_t *payload) {
    /* Extract expected CRC32 */
    ps->expected_crc = ((uint32_t)payload[0] << 24) |
                       ((uint32_t)payload[1] << 16) |
                       ((uint32_t)payload[2] << 8) |
                       ((uint32_t)payload[3]);
}

static void handle_data_block(struct perf_state *ps, const uint8_t *payload, uint32_t length) {
    uint32_t calculated_crc;
    uint8_t response[4];

    ps->bytes_rx += length;
    ps->packets_rx++;

    /* Validate CRC */
    calculated_crc = calculate_crc32(payload, length);

    if (calculated_crc != ps->expected_crc) {
        ps->errors++;
        send_message(ps->pcb, MSG_ERROR, NULL, 0);
        return;
    }

    /* Generate random response data in global buffer */
    for (uint32_t i = 0; i < ps->block_size; i++) {
        g_test_buffer[i] = (uint8_t)(rand() & 0xFF);
    }

    /* Calculate CRC of response */
    calculated_crc = calculate_crc32(g_test_buffer, ps->block_size);

    /* Send CRC */
    response[0] = (calculated_crc >> 24) & 0xFF;
    response[1] = (calculated_crc >> 16) & 0xFF;
    response[2] = (calculated_crc >> 8) & 0xFF;
    response[3] = calculated_crc & 0xFF;

    send_message(ps->pcb, MSG_DATA_CRC, response, 4);

    /* Send data block */
    send_message(ps->pcb, MSG_DATA_BLOCK, g_test_buffer, ps->block_size);

    ps->bytes_tx += ps->block_size;
    ps->packets_tx++;
}

static void handle_test_stop(struct perf_state *ps) {
    ps->test_active = 0;
}

//==============================================================================
// TCP Callbacks
//==============================================================================

static err_t perf_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    struct perf_state *ps = (struct perf_state *)arg;
    uint8_t header[8];
    uint8_t payload_buf[256];
    uint32_t msg_type, msg_length, msg_total;
    u16_t copied;
    struct pbuf *combined;

    if (p == NULL) {
        tcp_close(tpcb);
        if (ps) {
            if (ps->pending) pbuf_free(ps->pending);
            free(ps);
        }
        return ERR_OK;
    }

    if (err != ERR_OK) {
        pbuf_free(p);
        return err;
    }

    /* Chain new data to pending */
    if (ps->pending) {
        pbuf_cat(ps->pending, p);
        combined = ps->pending;
    } else {
        combined = p;
    }

    /* Try to process complete messages */
    while (combined && combined->tot_len >= 8) {
        /* Read header */
        copied = pbuf_copy_partial(combined, header, 8, 0);
        if (copied != 8) break;

        /* Decode message */
        msg_type = ((uint32_t)header[0] << 24) |
                   ((uint32_t)header[1] << 16) |
                   ((uint32_t)header[2] << 8) |
                   ((uint32_t)header[3]);

        msg_length = ((uint32_t)header[4] << 24) |
                     ((uint32_t)header[5] << 16) |
                     ((uint32_t)header[6] << 8) |
                     ((uint32_t)header[7]);

        msg_total = 8 + msg_length;

        /* Do we have the complete message? */
        if (combined->tot_len < msg_total) {
            /* Incomplete - save and wait for more data */
            ps->pending = combined;
            return ERR_OK;
        }

        /* We have complete message - process it */
        switch (msg_type) {
            case MSG_CAPS_REQ:
                handle_caps_req(ps);
                break;

            case MSG_TEST_START:
                if (msg_length >= 4 && msg_length <= sizeof(payload_buf)) {
                    copied = pbuf_copy_partial(combined, payload_buf, msg_length, 8);
                    if (copied == msg_length) {
                        handle_test_start(ps, payload_buf);
                    }
                }
                break;

            case MSG_DATA_CRC:
                if (msg_length >= 4 && msg_length <= sizeof(payload_buf)) {
                    copied = pbuf_copy_partial(combined, payload_buf, msg_length, 8);
                    if (copied == msg_length) {
                        handle_data_crc(ps, payload_buf);
                    }
                }
                break;

            case MSG_DATA_BLOCK:
                if (msg_length > 0 && msg_length <= g_buffer_size) {
                    copied = pbuf_copy_partial(combined, g_test_buffer, msg_length, 8);
                    if (copied == msg_length) {
                        handle_data_block(ps, g_test_buffer, msg_length);
                    }
                }
                break;

            case MSG_TEST_STOP:
                handle_test_stop(ps);
                break;

            default:
                break;
        }

        /* Remove processed message from buffer */
        if (combined->tot_len == msg_total) {
            /* Exact match - all done */
            tcp_recved(tpcb, combined->tot_len);
            pbuf_free(combined);
            ps->pending = NULL;
            return ERR_OK;
        } else {
            /* More data remains - need to split */
            struct pbuf *next_msg = pbuf_alloc(PBUF_RAW, combined->tot_len - msg_total, PBUF_RAM);
            if (next_msg) {
                pbuf_copy_partial(combined, next_msg->payload, combined->tot_len - msg_total, msg_total);
                tcp_recved(tpcb, msg_total);
                pbuf_free(combined);
                combined = next_msg;
                ps->pending = combined;
            } else {
                /* Allocation failed - drop everything */
                tcp_recved(tpcb, combined->tot_len);
                pbuf_free(combined);
                ps->pending = NULL;
                return ERR_OK;
            }
        }
    }

    /* Less than 8 bytes - save for later */
    if (combined && combined->tot_len > 0) {
        ps->pending = combined;
    } else {
        ps->pending = NULL;
        if (combined) pbuf_free(combined);
    }

    return ERR_OK;
}

static void perf_err(void *arg, err_t err) {
    struct perf_state *ps = (struct perf_state *)arg;

    (void)err;  /* Unused */

    if (ps) {
        free(ps);
    }
}

static err_t perf_accept(void *arg, struct tcp_pcb *newpcb, err_t err) {
    struct perf_state *ps;

    (void)arg;

    if (err != ERR_OK || newpcb == NULL) {
        return ERR_VAL;
    }

    /* Allocate state */
    ps = (struct perf_state *)malloc(sizeof(struct perf_state));
    if (!ps) {
        return ERR_MEM;
    }

    memset(ps, 0, sizeof(struct perf_state));
    ps->pcb = newpcb;

    tcp_arg(newpcb, ps);
    tcp_recv(newpcb, perf_recv);
    tcp_err(newpcb, perf_err);

    return ERR_OK;
}

//==============================================================================
// Network and Server Initialization
//==============================================================================

static struct netif slip_netif;

static void network_init(void) {
    ip4_addr_t ipaddr, netmask, gw;

    printf("\r\n");
    printf("=========================================\r\n");
    printf("lwIP Performance Test Server\r\n");
    printf("=========================================\r\n");
    printf("Version: %s\r\n", LWIP_VERSION_STRING);
    printf("\r\n");

    lwip_init();

    ip4addr_aton(DEVICE_IP, &ipaddr);
    ip4addr_aton(NETMASK, &netmask);
    ip4addr_aton(GATEWAY_IP, &gw);

    printf("Network Configuration:\r\n");
    printf("  IP address: %s\r\n", DEVICE_IP);
    printf("  Netmask:    %s\r\n", NETMASK);
    printf("  Gateway:    %s\r\n", GATEWAY_IP);
    printf("  Max buffer: %d KB\r\n", MAX_BUFFER_SIZE / 1024);
    printf("\r\n");

    netif_add(&slip_netif, &ipaddr, &netmask, &gw, NULL, slipif_init, ip_input);
    netif_set_default(&slip_netif);
    netif_set_up(&slip_netif);
    netif_set_link_up(&slip_netif);

    printf("  SLIP ready\r\n");
    printf("\r\n");
}

static void perf_server_init(void) {
    struct tcp_pcb *pcb;

    pcb = tcp_new();
    if (!pcb) {
        printf("Failed to create TCP PCB!\r\n");
        return;
    }

    if (tcp_bind(pcb, IP_ADDR_ANY, PERF_PORT) != ERR_OK) {
        printf("TCP bind failed!\r\n");
        tcp_close(pcb);
        return;
    }

    pcb = tcp_listen(pcb);
    if (!pcb) {
        printf("TCP listen failed!\r\n");
        return;
    }

    tcp_accept(pcb, perf_accept);

    printf("Performance test server listening on port %d\r\n", PERF_PORT);
    printf("\r\n");
    printf("Waiting for client connection...\r\n");
    printf("\r\n");
}

//==============================================================================
// Main
//==============================================================================

int main(void) {
    printf("\r\n");
    printf("==========================================\r\n");
    printf("PicoRV32 SLIP Performance Test Server\r\n");
    printf("==========================================\r\n");
    printf("\r\n");

    /* Allocate test buffer before starting SLIP */
    printf("Memory Allocation:\r\n");
    printf("  Allocating %d KB test buffer...", MAX_BUFFER_SIZE / 1024);
    g_test_buffer = (uint8_t *)malloc(MAX_BUFFER_SIZE);
    if (!g_test_buffer) {
        printf(" FAILED!\r\n");
        printf("  Error: Could not allocate buffer!\r\n");
        printf("\r\n");
        printf("System halted.\r\n");
        while (1) { }  /* Halt on failure */
    }
    g_buffer_size = MAX_BUFFER_SIZE;
    printf(" OK\r\n");
    printf("  Buffer address: 0x%08lx\r\n", (unsigned long)g_test_buffer);
    printf("  Buffer size:    %lu bytes (%lu KB)\r\n",
           (unsigned long)g_buffer_size, (unsigned long)(g_buffer_size / 1024));
    printf("\r\n");

    crc32_init();
    network_init();
    perf_server_init();

    printf("Ready to accept connections!\r\n");
    printf("Disconnect terminal and start SLIP now.\r\n");
    printf("\r\n");

    while (1) {
        slipif_poll(&slip_netif);
        sys_check_timeouts();
    }

    return 0;
}
