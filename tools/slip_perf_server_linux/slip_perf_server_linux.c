//===============================================================================
// SLIP Performance Test Server - Linux Version for Local Testing
//
// This is a standard Linux TCP server that implements the exact same protocol
// as the embedded lwIP server. Use this for rapid debugging without FPGA.
//
// Benefits:
// - Instant testing (no FPGA upload required)
// - printf debugging works
// - gdb debugging works
// - Fast iteration
//
// Protocol:
//   Port 8888
//   Message format: [Type:4][Length:4][Payload:N]
//
// Usage:
//   gcc -o slip_perf_server_linux slip_perf_server_linux.c -Wall -Wextra
//   ./slip_perf_server_linux
//
//   # In another terminal:
//   tools/slip_perf_client/slip_perf_client 127.0.0.1
//
// Copyright (c) October 2025 Michael Wolak
// Email: mikewolak@gmail.com, mike@epromfoundry.com
//===============================================================================

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

//==============================================================================
// Configuration
//==============================================================================

#define PERF_PORT       8888
#define MAX_BUFFER_SIZE (240 * 1024)  // 240KB (matches embedded server)

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

static uint8_t *g_test_buffer = NULL;
static uint32_t g_buffer_size = 0;

struct perf_state {
    uint32_t block_size;
    uint32_t bytes_rx;
    uint32_t bytes_tx;
    uint32_t packets_rx;
    uint32_t packets_tx;
    uint32_t errors;
    uint8_t test_active;
    uint32_t expected_crc;
};

//==============================================================================
// Helper Functions
//==============================================================================

static int send_all(int sockfd, const void *buf, size_t len) {
    const uint8_t *ptr = buf;
    size_t remaining = len;

    while (remaining > 0) {
        ssize_t sent = send(sockfd, ptr, remaining, 0);
        if (sent <= 0) {
            if (sent < 0) {
                perror("send");
            }
            return -1;
        }
        ptr += sent;
        remaining -= sent;
    }

    return 0;
}

static int recv_all(int sockfd, void *buf, size_t len) {
    uint8_t *ptr = buf;
    size_t remaining = len;

    while (remaining > 0) {
        ssize_t received = recv(sockfd, ptr, remaining, 0);
        if (received <= 0) {
            if (received < 0) {
                perror("recv");
            }
            return -1;
        }
        ptr += received;
        remaining -= received;
    }

    return 0;
}

static int send_message(int sockfd, uint32_t type, const void *payload, uint32_t length) {
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

    printf("[TX] Message type=0x%02X length=%u\n", type, length);
    printf("[TX] Header bytes: %02X %02X %02X %02X %02X %02X %02X %02X\n",
           header[0], header[1], header[2], header[3],
           header[4], header[5], header[6], header[7]);

    /* Send header */
    if (send_all(sockfd, header, 8) < 0) {
        return -1;
    }

    /* Send payload if present */
    if (payload && length > 0) {
        if (length <= 16) {
            const uint8_t *p = payload;
            printf("[TX] Payload bytes:");
            for (uint32_t i = 0; i < length; i++) {
                printf(" %02X", p[i]);
            }
            printf("\n");
        }

        if (send_all(sockfd, payload, length) < 0) {
            return -1;
        }
    }

    return 0;
}

//==============================================================================
// Protocol Handlers
//==============================================================================

static void handle_caps_req(int sockfd) {
    uint8_t payload[4];

    printf("\n=== CAPS_REQ ===\n");
    printf("Sending buffer size: %u bytes (%u KB)\n", g_buffer_size, g_buffer_size / 1024);

    /* Send actual allocated buffer size */
    payload[0] = (g_buffer_size >> 24) & 0xFF;
    payload[1] = (g_buffer_size >> 16) & 0xFF;
    payload[2] = (g_buffer_size >> 8) & 0xFF;
    payload[3] = g_buffer_size & 0xFF;

    send_message(sockfd, MSG_CAPS_RESP, payload, 4);
}

static void handle_test_start(int sockfd, struct perf_state *ps, const uint8_t *payload) {
    uint32_t block_size;
    uint8_t error_payload[12];

    printf("\n=== TEST_START ===\n");
    printf("Payload bytes: %02X %02X %02X %02X\n",
           payload[0], payload[1], payload[2], payload[3]);

    /* Extract block size from payload */
    block_size = ((uint32_t)payload[0] << 24) |
                 ((uint32_t)payload[1] << 16) |
                 ((uint32_t)payload[2] << 8) |
                 ((uint32_t)payload[3]);

    printf("Requested block size: %u bytes (%u KB)\n", block_size, block_size / 1024);
    printf("Server g_buffer_size: %u bytes (%u KB)\n", g_buffer_size, g_buffer_size / 1024);
    printf("Comparison: %u > %u = %s\n",
           block_size, g_buffer_size,
           block_size > g_buffer_size ? "TRUE (will reject)" : "FALSE (will accept)");

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

        printf("ERROR: Sending rejection (size too large)\n");
        send_message(sockfd, MSG_ERROR, error_payload, 12);
        return;
    }

    /* Use pre-allocated global buffer */
    if (!g_test_buffer) {
        /* Send error with reason code 2 = buffer not allocated */
        error_payload[0] = 0;
        error_payload[1] = 0;
        error_payload[2] = 0;
        error_payload[3] = 2;  /* Error code: 2 = buffer not available */
        printf("ERROR: Buffer not allocated\n");
        send_message(sockfd, MSG_ERROR, error_payload, 4);
        return;
    }

    ps->block_size = block_size;
    ps->test_active = 1;
    ps->bytes_rx = 0;
    ps->bytes_tx = 0;
    ps->packets_rx = 0;
    ps->packets_tx = 0;
    ps->errors = 0;

    printf("SUCCESS: Sending TEST_ACK\n");
    send_message(sockfd, MSG_TEST_ACK, NULL, 0);
}

static void handle_data_crc(struct perf_state *ps, const uint8_t *payload) {
    /* Extract expected CRC32 */
    ps->expected_crc = ((uint32_t)payload[0] << 24) |
                       ((uint32_t)payload[1] << 16) |
                       ((uint32_t)payload[2] << 8) |
                       ((uint32_t)payload[3]);

    printf("\n=== DATA_CRC ===\n");
    printf("Expected CRC: 0x%08X\n", ps->expected_crc);
}

static void handle_data_block(int sockfd, struct perf_state *ps, const uint8_t *payload, uint32_t length) {
    uint32_t calculated_crc;
    uint8_t response[4];

    printf("\n=== DATA_BLOCK ===\n");
    printf("Received %u bytes\n", length);

    ps->bytes_rx += length;
    ps->packets_rx++;

    /* Validate CRC */
    calculated_crc = calculate_crc32(payload, length);
    printf("Calculated CRC: 0x%08X\n", calculated_crc);
    printf("Expected CRC:   0x%08X\n", ps->expected_crc);

    if (calculated_crc != ps->expected_crc) {
        ps->errors++;
        printf("ERROR: CRC mismatch!\n");
        send_message(sockfd, MSG_ERROR, NULL, 0);
        return;
    }

    printf("CRC OK\n");

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

    printf("Sending response CRC: 0x%08X\n", calculated_crc);
    send_message(sockfd, MSG_DATA_CRC, response, 4);

    /* Send data block */
    printf("Sending %u byte response\n", ps->block_size);
    send_message(sockfd, MSG_DATA_BLOCK, g_test_buffer, ps->block_size);

    ps->bytes_tx += ps->block_size;
    ps->packets_tx++;
}

static void handle_test_stop(struct perf_state *ps) {
    printf("\n=== TEST_STOP ===\n");
    ps->test_active = 0;
}

//==============================================================================
// Client Handler
//==============================================================================

static void handle_client(int client_sock) {
    struct perf_state ps;
    uint8_t header[8];
    uint8_t *payload_buf = NULL;
    uint32_t msg_type, msg_length;

    memset(&ps, 0, sizeof(ps));

    printf("\n========================================\n");
    printf("Client connected\n");
    printf("========================================\n");

    /* Allocate payload buffer (large enough for any message) */
    payload_buf = malloc(MAX_BUFFER_SIZE);
    if (!payload_buf) {
        fprintf(stderr, "Failed to allocate payload buffer\n");
        return;
    }

    while (1) {
        /* Receive header */
        if (recv_all(client_sock, header, 8) < 0) {
            printf("Client disconnected\n");
            break;
        }

        /* Extract message type and length */
        msg_type = ((uint32_t)header[0] << 24) |
                   ((uint32_t)header[1] << 16) |
                   ((uint32_t)header[2] << 8) |
                   ((uint32_t)header[3]);

        msg_length = ((uint32_t)header[4] << 24) |
                     ((uint32_t)header[5] << 16) |
                     ((uint32_t)header[6] << 8) |
                     ((uint32_t)header[7]);

        printf("\n[RX] Message type=0x%02X length=%u\n", msg_type, msg_length);
        printf("[RX] Header bytes: %02X %02X %02X %02X %02X %02X %02X %02X\n",
               header[0], header[1], header[2], header[3],
               header[4], header[5], header[6], header[7]);

        /* Validate message length */
        if (msg_length > MAX_BUFFER_SIZE) {
            fprintf(stderr, "ERROR: Message length %u exceeds maximum %u\n",
                   msg_length, MAX_BUFFER_SIZE);
            break;
        }

        /* Receive payload if present */
        if (msg_length > 0) {
            if (recv_all(client_sock, payload_buf, msg_length) < 0) {
                printf("Failed to receive payload\n");
                break;
            }

            if (msg_length <= 16) {
                printf("[RX] Payload bytes:");
                for (uint32_t i = 0; i < msg_length; i++) {
                    printf(" %02X", payload_buf[i]);
                }
                printf("\n");
            }
        }

        /* Handle message based on type */
        switch (msg_type) {
            case MSG_CAPS_REQ:
                handle_caps_req(client_sock);
                break;

            case MSG_TEST_START:
                if (msg_length >= 4) {
                    handle_test_start(client_sock, &ps, payload_buf);
                }
                break;

            case MSG_DATA_CRC:
                if (msg_length >= 4) {
                    handle_data_crc(&ps, payload_buf);
                }
                break;

            case MSG_DATA_BLOCK:
                handle_data_block(client_sock, &ps, payload_buf, msg_length);
                break;

            case MSG_TEST_STOP:
                handle_test_stop(&ps);
                break;

            default:
                printf("WARNING: Unknown message type 0x%02X\n", msg_type);
                break;
        }
    }

    free(payload_buf);
    printf("\n========================================\n");
    printf("Client handler finished\n");
    printf("========================================\n");
}

//==============================================================================
// Main
//==============================================================================

int main(void) {
    int server_sock, client_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    int reuse = 1;

    printf("\n==========================================\n");
    printf("SLIP Performance Test Server - Linux\n");
    printf("==========================================\n");
    printf("\n");

    /* Allocate test buffer */
    printf("Memory Allocation:\n");
    printf("  Allocating %d KB test buffer...", MAX_BUFFER_SIZE / 1024);
    g_test_buffer = (uint8_t *)malloc(MAX_BUFFER_SIZE);
    if (!g_test_buffer) {
        fprintf(stderr, " FAILED!\n");
        fprintf(stderr, "  Error: Could not allocate buffer!\n");
        return 1;
    }
    g_buffer_size = MAX_BUFFER_SIZE;
    printf(" OK\n");
    printf("  Buffer address: %p\n", (void*)g_test_buffer);
    printf("  Buffer size:    %u bytes (%u KB)\n", g_buffer_size, g_buffer_size / 1024);
    printf("\n");

    crc32_init();

    /* Create socket */
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("socket");
        free(g_test_buffer);
        return 1;
    }

    /* Set SO_REUSEADDR to avoid "Address already in use" */
    if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        perror("setsockopt");
        close(server_sock);
        free(g_test_buffer);
        return 1;
    }

    /* Bind to port */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PERF_PORT);

    if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(server_sock);
        free(g_test_buffer);
        return 1;
    }

    /* Listen */
    if (listen(server_sock, 1) < 0) {
        perror("listen");
        close(server_sock);
        free(g_test_buffer);
        return 1;
    }

    printf("Server Configuration:\n");
    printf("  Port:       %d\n", PERF_PORT);
    printf("  Max buffer: %d KB\n", MAX_BUFFER_SIZE / 1024);
    printf("\n");
    printf("Ready to accept connections!\n");
    printf("Waiting for client...\n");
    printf("\n");
    printf("Test with:\n");
    printf("  tools/slip_perf_client/slip_perf_client 127.0.0.1 -d 2\n");
    printf("\n");

    /* Accept and handle clients */
    while (1) {
        client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &client_len);
        if (client_sock < 0) {
            perror("accept");
            continue;
        }

        printf("Connection from %s:%d\n",
               inet_ntoa(client_addr.sin_addr),
               ntohs(client_addr.sin_port));

        handle_client(client_sock);

        close(client_sock);
        printf("\nWaiting for next client...\n\n");
    }

    close(server_sock);
    free(g_test_buffer);

    return 0;
}
