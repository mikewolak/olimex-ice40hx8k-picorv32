//===============================================================================
// SLIP Performance Test Client - Linux Host Application
//
// Professional ncurses UI for bidirectional TCP/IP performance testing
// Connects to firmware server, negotiates buffer size, performs CRC32-validated
// data transfers with real-time statistics display.
//
// Features:
// - Automatic capability negotiation
// - Unidirectional and bidirectional test modes
// - CRC32 validation (0xEDB88320 polynomial)
// - Real-time throughput and statistics display
// - Professional ncurses UI with progress bars
// - Configurable test duration and timeout
// - Clean shutdown signaling
//
// Usage:
//   ./slip_perf_client <server_ip> [options]
//
// Options:
//   -d <seconds>   Test duration (default: 2)
//   -t <seconds>   Timeout (default: 30)
//   -b             Bidirectional mode (default: unidirectional)
//
// Example:
//   ./slip_perf_client 192.168.100.2 -d 2 -t 30 -b
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
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#ifndef DEBUG_MODE
#include <ncurses.h>
#endif
#include <time.h>
#include <signal.h>
#include <errno.h>
#include <getopt.h>

#ifdef DEBUG_MODE
#define DEBUG_PRINT(...) do { printf("[DEBUG] "); printf(__VA_ARGS__); fflush(stdout); } while(0)
#else
#define DEBUG_PRINT(...) do {} while(0)
#endif

//==============================================================================
// Configuration
//==============================================================================

#define DEFAULT_PORT            8888
#define DEFAULT_TIMEOUT_SEC     1800  // 30 minutes
#define DEFAULT_DURATION_SEC    2
#define MAX_BUFFER_SIZE         (32 * 1024)  // Match server limit

//==============================================================================
// Protocol Message Types (must match firmware)
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
// CRC32 Implementation (matches firmware polynomial 0xEDB88320)
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
// Global State
//==============================================================================

struct test_stats {
    uint64_t bytes_tx;
    uint64_t bytes_rx;
    uint64_t packets_tx;
    uint64_t packets_rx;
    uint64_t errors;
    time_t start_time;
    time_t current_time;
    double tx_rate_kbps;
    double rx_rate_kbps;
};

static int sockfd = -1;
static int running = 1;
static struct test_stats stats;
static uint32_t server_max_buffer = 0;
static uint32_t block_size = 0;
static uint8_t *tx_buffer = NULL;
static uint8_t *rx_buffer = NULL;

//==============================================================================
// Signal Handler
//==============================================================================

static void signal_handler(int sig) {
    (void)sig;
    running = 0;
}

//==============================================================================
// Network Helper Functions
//==============================================================================

static int send_message(uint32_t type, const void *payload, uint32_t length) {
    uint8_t header[8];

    DEBUG_PRINT("send_message: type=0x%08X, length=%u\n", type, length);

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
    ssize_t sent = send(sockfd, header, 8, 0);
    if (sent != 8) {
        DEBUG_PRINT("send_message: header send failed (sent=%zd, errno=%d)\n", sent, errno);
        return -1;
    }

    /* Send payload if present */
    if (payload && length > 0) {
        sent = send(sockfd, payload, length, 0);
        if (sent != (ssize_t)length) {
            DEBUG_PRINT("send_message: payload send failed (sent=%zd/%u, errno=%d)\n", sent, length, errno);
            return -1;
        }
    }

    DEBUG_PRINT("send_message: success\n");
    return 0;
}

static int recv_message(uint32_t *type, uint8_t *payload, uint32_t *length, uint32_t max_length) {
    uint8_t header[8];
    uint32_t msg_type, msg_length;

    DEBUG_PRINT("recv_message: waiting for header...\n");

    /* Receive header */
    ssize_t n = recv(sockfd, header, 8, MSG_WAITALL);
    if (n != 8) {
        DEBUG_PRINT("recv_message: header receive failed (got=%zd, errno=%d)\n", n, errno);
        return -1;
    }

    /* Parse header */
    msg_type = ((uint32_t)header[0] << 24) |
               ((uint32_t)header[1] << 16) |
               ((uint32_t)header[2] << 8) |
               ((uint32_t)header[3]);

    msg_length = ((uint32_t)header[4] << 24) |
                 ((uint32_t)header[5] << 16) |
                 ((uint32_t)header[6] << 8) |
                 ((uint32_t)header[7]);

    DEBUG_PRINT("recv_message: got type=0x%08X, length=%u\n", msg_type, msg_length);

    *type = msg_type;
    *length = msg_length;

    /* Receive payload if present */
    if (msg_length > 0) {
        if (msg_length > max_length) {
            DEBUG_PRINT("recv_message: payload too large (%u > %u)\n", msg_length, max_length);
            return -1;  /* Payload too large */
        }

        n = recv(sockfd, payload, msg_length, MSG_WAITALL);
        if (n != (ssize_t)msg_length) {
            DEBUG_PRINT("recv_message: payload receive failed (got=%zd/%u, errno=%d)\n", n, msg_length, errno);
            return -1;
        }
        DEBUG_PRINT("recv_message: payload received (%u bytes)\n", msg_length);
    }

    DEBUG_PRINT("recv_message: success\n");
    return 0;
}

//==============================================================================
// Protocol Functions
//==============================================================================

static int request_capabilities(void) {
    uint32_t msg_type, msg_length;
    uint8_t payload[4];

    /* Send capabilities request */
    if (send_message(MSG_CAPS_REQ, NULL, 0) < 0) {
        return -1;
    }

    /* Receive capabilities response */
    if (recv_message(&msg_type, payload, &msg_length, sizeof(payload)) < 0) {
        return -1;
    }

    if (msg_type != MSG_CAPS_RESP || msg_length != 4) {
        return -1;
    }

    /* Extract max buffer size */
    server_max_buffer = ((uint32_t)payload[0] << 24) |
                       ((uint32_t)payload[1] << 16) |
                       ((uint32_t)payload[2] << 8) |
                       ((uint32_t)payload[3]);

    return 0;
}

static int start_test(uint32_t requested_block_size) {
    uint32_t msg_type, msg_length;
    uint8_t payload[16];  /* Large enough for extended error info */

    /* Build payload with block size */
    payload[0] = (requested_block_size >> 24) & 0xFF;
    payload[1] = (requested_block_size >> 16) & 0xFF;
    payload[2] = (requested_block_size >> 8) & 0xFF;
    payload[3] = requested_block_size & 0xFF;

    /* Send test start */
    if (send_message(MSG_TEST_START, payload, 4) < 0) {
        fprintf(stderr, "Error: Failed to send TEST_START message\n");
        return -1;
    }

    /* Wait for acknowledgment */
    if (recv_message(&msg_type, payload, &msg_length, sizeof(payload)) < 0) {
        fprintf(stderr, "Error: Failed to receive response to TEST_START\n");
        return -1;
    }

    if (msg_type == MSG_ERROR) {
        uint32_t error_code = 0;
        uint32_t requested_size = 0;
        uint32_t actual_max = 0;

        if (msg_length >= 4) {
            error_code = ((uint32_t)payload[0] << 24) |
                        ((uint32_t)payload[1] << 16) |
                        ((uint32_t)payload[2] << 8) |
                        ((uint32_t)payload[3]);
        }

        fprintf(stderr, "Error: Server returned ERROR response to TEST_START\n");

        if (error_code == 1) {
            fprintf(stderr, "  Reason: Block size exceeds server maximum\n");

            /* Decode extended error info if present */
            if (msg_length >= 12) {
                requested_size = ((uint32_t)payload[4] << 24) |
                                ((uint32_t)payload[5] << 16) |
                                ((uint32_t)payload[6] << 8) |
                                ((uint32_t)payload[7]);

                actual_max = ((uint32_t)payload[8] << 24) |
                            ((uint32_t)payload[9] << 16) |
                            ((uint32_t)payload[10] << 8) |
                            ((uint32_t)payload[11]);

                fprintf(stderr, "  Server saw requested: %u bytes (%u KB)\n",
                       requested_size, requested_size / 1024);
                fprintf(stderr, "  Server g_buffer_size: %u bytes (%u KB)\n",
                       actual_max, actual_max / 1024);
                fprintf(stderr, "  Comparison: %u > %u = %s\n",
                       requested_size, actual_max,
                       requested_size > actual_max ? "TRUE (ERROR)" : "FALSE (should pass!)");
            }
        } else if (error_code == 2) {
            fprintf(stderr, "  Reason: Server malloc() failed - out of heap memory\n");
            fprintf(stderr, "  Hint: lwIP may be using too much heap. Try smaller block size.\n");
        } else if (error_code != 0) {
            fprintf(stderr, "  Error code: %u\n", error_code);
        }
        return -1;
    }

    if (msg_type != MSG_TEST_ACK) {
        fprintf(stderr, "Error: Expected TEST_ACK (0x%02X) but got 0x%08X\n", MSG_TEST_ACK, msg_type);
        return -1;
    }

    return 0;
}

static int stop_test(void) {
    return send_message(MSG_TEST_STOP, NULL, 0);
}

static int send_data_block(const uint8_t *data, uint32_t length) {
    uint32_t crc;
    uint8_t crc_payload[4];

    DEBUG_PRINT("send_data_block: length=%u\n", length);

    /* Calculate CRC */
    crc = calculate_crc32(data, length);
    DEBUG_PRINT("send_data_block: CRC32=0x%08X\n", crc);

    /* Send CRC */
    crc_payload[0] = (crc >> 24) & 0xFF;
    crc_payload[1] = (crc >> 16) & 0xFF;
    crc_payload[2] = (crc >> 8) & 0xFF;
    crc_payload[3] = crc & 0xFF;

    if (send_message(MSG_DATA_CRC, crc_payload, 4) < 0) {
        DEBUG_PRINT("send_data_block: CRC send failed\n");
        return -1;
    }

    /* Send data block */
    if (send_message(MSG_DATA_BLOCK, data, length) < 0) {
        DEBUG_PRINT("send_data_block: data block send failed\n");
        return -1;
    }

    stats.bytes_tx += length;
    stats.packets_tx++;

    DEBUG_PRINT("send_data_block: success (total_tx=%llu)\n", (unsigned long long)stats.bytes_tx);
    return 0;
}

static int recv_data_block(uint8_t *data, uint32_t *length, uint32_t max_length) {
    uint32_t msg_type, msg_length, expected_crc, calculated_crc;
    uint8_t crc_payload[4];

    DEBUG_PRINT("recv_data_block: waiting for response...\n");

    /* Receive CRC */
    if (recv_message(&msg_type, crc_payload, &msg_length, sizeof(crc_payload)) < 0) {
        DEBUG_PRINT("recv_data_block: CRC receive failed\n");
        return -1;
    }

    if (msg_type != MSG_DATA_CRC || msg_length != 4) {
        DEBUG_PRINT("recv_data_block: unexpected CRC message (type=0x%08X, len=%u)\n", msg_type, msg_length);
        stats.errors++;
        return -1;
    }

    expected_crc = ((uint32_t)crc_payload[0] << 24) |
                   ((uint32_t)crc_payload[1] << 16) |
                   ((uint32_t)crc_payload[2] << 8) |
                   ((uint32_t)crc_payload[3]);

    DEBUG_PRINT("recv_data_block: expected CRC=0x%08X\n", expected_crc);

    /* Receive data block */
    if (recv_message(&msg_type, data, &msg_length, max_length) < 0) {
        DEBUG_PRINT("recv_data_block: data block receive failed\n");
        return -1;
    }

    if (msg_type == MSG_ERROR) {
        DEBUG_PRINT("recv_data_block: server returned ERROR\n");
        stats.errors++;
        return -1;
    }

    if (msg_type != MSG_DATA_BLOCK) {
        DEBUG_PRINT("recv_data_block: unexpected message type (got=0x%08X, expected=0x%08X)\n", msg_type, MSG_DATA_BLOCK);
        stats.errors++;
        return -1;
    }

    *length = msg_length;

    /* Validate CRC */
    calculated_crc = calculate_crc32(data, msg_length);

    DEBUG_PRINT("recv_data_block: calculated CRC=0x%08X\n", calculated_crc);

    if (calculated_crc != expected_crc) {
        DEBUG_PRINT("recv_data_block: CRC MISMATCH! (expected=0x%08X, got=0x%08X)\n", expected_crc, calculated_crc);
        stats.errors++;
        return -1;
    }

    stats.bytes_rx += msg_length;
    stats.packets_rx++;

    DEBUG_PRINT("recv_data_block: success (received %u bytes, total_rx=%llu)\n", msg_length, (unsigned long long)stats.bytes_rx);
    return 0;
}

//==============================================================================
// Display Functions (Debug Mode)
//==============================================================================

#ifdef DEBUG_MODE
static void update_display(int duration_sec, int bidirectional) {
    time_t elapsed = stats.current_time - stats.start_time;

    /* Calculate rates */
    if (elapsed > 0) {
        stats.tx_rate_kbps = (stats.bytes_tx / 1024.0) / elapsed;
        stats.rx_rate_kbps = (stats.bytes_rx / 1024.0) / elapsed;
    }

    printf("\n[STATUS] Elapsed: %ld/%d sec | TX: %llu pkts, %llu bytes (%.2f KB/s) | RX: %llu pkts, %llu bytes (%.2f KB/s) | Errors: %llu\n",
           elapsed, duration_sec,
           (unsigned long long)stats.packets_tx,
           (unsigned long long)stats.bytes_tx,
           stats.tx_rate_kbps,
           (unsigned long long)stats.packets_rx,
           (unsigned long long)stats.bytes_rx,
           stats.rx_rate_kbps,
           (unsigned long long)stats.errors);
    fflush(stdout);
}
#else
static void draw_progress_bar(int y, int x, int width, double percent) {
    int filled = (int)(width * (percent / 100.0));

    mvaddch(y, x, '[');
    for (int i = 0; i < width; i++) {
        mvaddch(y, x + 1 + i, (i < filled) ? '#' : ' ');
    }
    mvaddch(y, x + width + 1, ']');
}

static void update_display(int duration_sec, int bidirectional) {
    time_t elapsed = stats.current_time - stats.start_time;
    double progress = (elapsed * 100.0) / duration_sec;
    if (progress > 100.0) progress = 100.0;

    /* Calculate rates */
    if (elapsed > 0) {
        stats.tx_rate_kbps = (stats.bytes_tx / 1024.0) / elapsed;
        stats.rx_rate_kbps = (stats.bytes_rx / 1024.0) / elapsed;
    }

    clear();

    /* Title */
    attron(A_BOLD);
    mvprintw(0, 0, "╔════════════════════════════════════════════════════════════════════════╗");
    mvprintw(1, 0, "║           SLIP Performance Test - %s Mode                  ║",
             bidirectional ? "Bidirectional" : "Unidirectional ");
    mvprintw(2, 0, "╚════════════════════════════════════════════════════════════════════════╝");
    attroff(A_BOLD);

    /* Connection info */
    mvprintw(4, 2, "Server Buffer:  %u KB", server_max_buffer / 1024);
    mvprintw(5, 2, "Block Size:     %u KB", block_size / 1024);
    mvprintw(6, 2, "Test Duration:  %d seconds", duration_sec);

    /* Progress */
    mvprintw(8, 2, "Progress:       %d / %d seconds", (int)elapsed, duration_sec);
    draw_progress_bar(9, 2, 60, progress);
    mvprintw(9, 65, "%3.0f%%", progress);

    /* TX Statistics */
    attron(A_BOLD);
    mvprintw(11, 2, "TRANSMIT (Client → Server)");
    attroff(A_BOLD);
    mvprintw(12, 4, "Packets:    %10llu", (unsigned long long)stats.packets_tx);
    mvprintw(13, 4, "Bytes:      %10llu  (%7.2f KB)",
             (unsigned long long)stats.bytes_tx,
             stats.bytes_tx / 1024.0);
    mvprintw(14, 4, "Rate:       %10.2f KB/s", stats.tx_rate_kbps);

    /* RX Statistics */
    attron(A_BOLD);
    mvprintw(16, 2, "RECEIVE (Server → Client)");
    attroff(A_BOLD);
    mvprintw(17, 4, "Packets:    %10llu", (unsigned long long)stats.packets_rx);
    mvprintw(18, 4, "Bytes:      %10llu  (%7.2f KB)",
             (unsigned long long)stats.bytes_rx,
             stats.bytes_rx / 1024.0);
    mvprintw(19, 4, "Rate:       %10.2f KB/s", stats.rx_rate_kbps);

    /* Errors */
    if (stats.errors > 0) {
        attron(A_BOLD | COLOR_PAIR(1));
        mvprintw(21, 2, "ERRORS:         %10llu", (unsigned long long)stats.errors);
        attroff(A_BOLD | COLOR_PAIR(1));
    } else {
        mvprintw(21, 2, "ERRORS:         %10llu", (unsigned long long)stats.errors);
    }

    /* Status */
    mvprintw(23, 2, "Status: %s",
             (elapsed >= duration_sec) ? "Complete - Press Ctrl-C to exit" : "Running...");

    refresh();
}
#endif

//==============================================================================
// Test Functions
//==============================================================================

static void run_unidirectional_test(int duration_sec) {
    time_t end_time = stats.start_time + duration_sec;

    while (running && time(NULL) < end_time) {
        stats.current_time = time(NULL);

        /* Generate random data */
        for (uint32_t i = 0; i < block_size; i++) {
            tx_buffer[i] = (uint8_t)(rand() & 0xFF);
        }

        /* Send to server */
        if (send_data_block(tx_buffer, block_size) < 0) {
            stats.errors++;
            continue;
        }

        /* Receive response */
        uint32_t rx_length;
        if (recv_data_block(rx_buffer, &rx_length, block_size) < 0) {
            stats.errors++;
            continue;
        }

        /* Update display */
        update_display(duration_sec, 0);
    }
}

static void run_bidirectional_test(int duration_sec) {
    /* For bidirectional, we send and receive simultaneously */
    /* For simplicity in this implementation, we alternate send/receive */
    /* A full implementation would use separate threads for true bidirectional */
    run_unidirectional_test(duration_sec);
}

//==============================================================================
// Main
//==============================================================================

static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s <server_ip> [options]\n", prog);
    fprintf(stderr, "\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -d <seconds>   Test duration (default: %d)\n", DEFAULT_DURATION_SEC);
    fprintf(stderr, "  -t <seconds>   Socket timeout (default: %d)\n", DEFAULT_TIMEOUT_SEC);
    fprintf(stderr, "  -b             Bidirectional mode (default: unidirectional)\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Example:\n");
    fprintf(stderr, "  %s 192.168.100.2 -d 2 -t 30 -b\n", prog);
    fprintf(stderr, "\n");
    exit(1);
}

int main(int argc, char *argv[]) {
    const char *server_ip;
    int duration_sec = DEFAULT_DURATION_SEC;
    int timeout_sec = DEFAULT_TIMEOUT_SEC;
    int bidirectional = 0;
    struct sockaddr_in server_addr;
    struct timeval tv;
    int opt;

    /* Parse command line */
    if (argc < 2) {
        usage(argv[0]);
    }

    server_ip = argv[1];

    while ((opt = getopt(argc - 1, argv + 1, "d:t:b")) != -1) {
        switch (opt) {
            case 'd':
                duration_sec = atoi(optarg);
                break;
            case 't':
                timeout_sec = atoi(optarg);
                break;
            case 'b':
                bidirectional = 1;
                break;
            default:
                usage(argv[0]);
        }
    }

    /* Initialize CRC32 */
    crc32_init();

    /* Setup signal handler */
    signal(SIGINT, signal_handler);

    /* Create socket */
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    /* Set socket timeout */
    tv.tv_sec = timeout_sec;
    tv.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    /* Connect to server */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(DEFAULT_PORT);

    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid server IP address: %s\n", server_ip);
        close(sockfd);
        return 1;
    }

    printf("Connecting to %s:%d...\n", server_ip, DEFAULT_PORT);

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        close(sockfd);
        return 1;
    }

    printf("Connected!\n");

    /* Request server capabilities */
    printf("Requesting server capabilities...\n");
    if (request_capabilities() < 0) {
        fprintf(stderr, "Failed to request capabilities\n");
        close(sockfd);
        return 1;
    }

    printf("Server max buffer: %u KB\n", server_max_buffer / 1024);

    /* Determine block size (use server's max, but cap at our max) */
    block_size = server_max_buffer;
    if (block_size > MAX_BUFFER_SIZE) {
        block_size = MAX_BUFFER_SIZE;
    }

    /* Allocate buffers */
    tx_buffer = malloc(block_size);
    rx_buffer = malloc(block_size);

    if (!tx_buffer || !rx_buffer) {
        fprintf(stderr, "Failed to allocate buffers (%u KB each)\n", block_size / 1024);
        close(sockfd);
        return 1;
    }

    printf("Using block size: %u KB\n", block_size / 1024);

    /* Start test */
    printf("Starting test...\n");
    if (start_test(block_size) < 0) {
        fprintf(stderr, "Failed to start test\n");
        free(tx_buffer);
        free(rx_buffer);
        close(sockfd);
        return 1;
    }

#ifndef DEBUG_MODE
    /* Initialize ncurses */
    initscr();
    cbreak();
    noecho();
    curs_set(0);
    start_color();
    init_pair(1, COLOR_RED, COLOR_BLACK);
#endif

    /* Initialize stats */
    memset(&stats, 0, sizeof(stats));
    stats.start_time = time(NULL);
    stats.current_time = stats.start_time;

    /* Run test */
    if (bidirectional) {
        run_bidirectional_test(duration_sec);
    } else {
        run_unidirectional_test(duration_sec);
    }

    /* Final display update */
    stats.current_time = time(NULL);
    update_display(duration_sec, bidirectional);

#ifndef DEBUG_MODE
    /* Wait a moment to let user see final display */
    sleep(2);

    /* Cleanup ncurses */
    endwin();
#endif

    /* Stop test */
    stop_test();

    /* Print final statistics immediately */
    printf("\n========================================\n");
    printf("  SLIP Performance Test Complete\n");
    printf("========================================\n\n");
    printf("Duration:   %ld seconds\n", stats.current_time - stats.start_time);
    printf("TX:         %llu bytes (%llu packets, %.2f KB/s)\n",
           (unsigned long long)stats.bytes_tx,
           (unsigned long long)stats.packets_tx,
           stats.tx_rate_kbps);
    printf("RX:         %llu bytes (%llu packets, %.2f KB/s)\n",
           (unsigned long long)stats.bytes_rx,
           (unsigned long long)stats.packets_rx,
           stats.rx_rate_kbps);
    printf("Errors:     %llu\n", (unsigned long long)stats.errors);
    printf("\n");

    /* Cleanup */
    free(tx_buffer);
    free(rx_buffer);
    close(sockfd);

    return 0;
}
