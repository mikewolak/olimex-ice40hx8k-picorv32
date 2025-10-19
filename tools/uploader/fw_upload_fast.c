//==============================================================================
// Olimex iCE40HX8K-EVB RISC-V Platform
// fw_upload_fast.c - FAST Streaming Firmware Uploader (NO chunking)
//
// Copyright (c) October 2025 Michael Wolak
// Email: mikewolak@gmail.com, mike@epromfoundry.com
//
// NOT FOR COMMERCIAL USE
// Educational and research purposes only
//==============================================================================

/*
 * IMPORTANT: This uploader uses a FAST streaming protocol that is ONLY
 * compatible with hexedit_fast.elf firmware. It is NOT compatible with the
 * standard bootloader protocol. Use the correct pairing:
 *   fw_upload_fast <-> hexedit_fast.elf (FAST streaming, NO chunking)
 *   fw_upload      <-> bootloader/hexedit.elf (standard chunked protocol)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

// Platform-specific includes
#ifdef _WIN32
    #include <windows.h>
    #include <setupapi.h>
    #include <devguid.h>
    #pragma comment(lib, "setupapi.lib")
    #define PLATFORM "Windows"
#else
    #include <unistd.h>
    #include <termios.h>
    #include <fcntl.h>
    #include <sys/ioctl.h>
    #include <sys/time.h>
    #ifndef FIONREAD
        #include <sys/socket.h>  // Try to get FIONREAD from socket.h
    #endif
    #ifndef FIONREAD
        #define FIONREAD 0x541B  // Define FIONREAD if still not available (Linux value)
    #endif
    #include <dirent.h>
    #ifdef __APPLE__
        #include <CoreFoundation/CoreFoundation.h>
        #include <IOKit/IOKitLib.h>
        #include <IOKit/serial/IOSerialKeys.h>
        #define PLATFORM "macOS"
    #else
        #define PLATFORM "Linux"
    #endif
#endif

// Configuration
#define DEFAULT_BAUD 1000000  // 1 Mbaud for FAST streaming
#define CHUNK_SIZE 64
#define MAX_PACKET_SIZE 524288  // 512KB to match SRAM size
#define TIMEOUT_MS 5000  // 5 seconds - enough for 512KB CRC calculation

// Color codes for terminal
#ifdef _WIN32
    // Disable colors on Windows or use plain text
    #define COLOR_RESET   ""
    #define COLOR_GREEN   ""
    #define COLOR_RED     ""
    #define COLOR_YELLOW  ""
    #define COLOR_BLUE    ""
    #define COLOR_CYAN    ""
    #define CHECK_MARK    "[OK]"
    #define CROSS_MARK    "[FAIL]"
#else
    // ANSI colors for Unix/Linux/Mac
    #define COLOR_RESET   "\033[0m"
    #define COLOR_GREEN   "\033[32m"
    #define COLOR_RED     "\033[31m"
    #define COLOR_YELLOW  "\033[33m"
    #define COLOR_BLUE    "\033[34m"
    #define COLOR_CYAN    "\033[36m"
    #define CHECK_MARK    "✓"
    #define CROSS_MARK    "✗"
#endif

// Progress display
typedef struct {
    size_t total_bytes;
    size_t bytes_sent;
    double start_time;
    bool verbose;
} progress_t;

// Serial port handle
#ifdef _WIN32
    typedef HANDLE serial_t;
    #define INVALID_SERIAL INVALID_HANDLE_VALUE
#else
    typedef int serial_t;
    #define INVALID_SERIAL -1
#endif

// CRC32 (PKZIP polynomial)
static uint32_t crc32_table[256];
static bool crc32_initialized = false;

void init_crc32(void) {
    if (crc32_initialized) return;

    for (int i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & (uint32_t)(-(int32_t)(crc & 1)));
        }
        crc32_table[i] = crc;
    }
    crc32_initialized = true;
}

uint32_t calculate_crc32(const uint8_t* data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; i++) {
        crc = (crc >> 8) ^ crc32_table[(crc ^ data[i]) & 0xFF];
    }
    return ~crc;
}

// Time utilities
double get_time(void) {
#ifdef _WIN32
    LARGE_INTEGER freq, count;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    return (double)count.QuadPart / (double)freq.QuadPart;
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1000000.0;
#endif
}

// Progress display
void show_progress(progress_t* prog) {
    if (prog->verbose) return;

    double elapsed = get_time() - prog->start_time;
    double rate = prog->bytes_sent / elapsed;
    double remaining = (prog->total_bytes - prog->bytes_sent) / rate;
    int percent = (int)(100.0 * prog->bytes_sent / prog->total_bytes);

    // Build progress bar
    char bar[52];
    int filled = percent / 2;
    for (int i = 0; i < 50; i++) {
        bar[i] = (i < filled) ? '=' : ' ';
    }
    bar[50] = '\0';

    // Clear line and show progress at bottom
    printf("\r" COLOR_CYAN "[%s] %3d%% | %zu/%zu bytes | %.1f KB/s | ETA: %.1fs" COLOR_RESET,
           bar, percent, prog->bytes_sent, prog->total_bytes,
           rate / 1024.0, remaining);
    fflush(stdout);
}

// Serial port functions
#ifdef _WIN32

serial_t serial_open(const char* port, int baud) {
    char full_port[32];
    snprintf(full_port, sizeof(full_port), "\\\\.\\%s", port);

    HANDLE h = CreateFileA(full_port, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                          OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return INVALID_SERIAL;

    DCB dcb = {0};
    dcb.DCBlength = sizeof(DCB);
    GetCommState(h, &dcb);
    dcb.BaudRate = baud;
    dcb.ByteSize = 8;
    dcb.StopBits = ONESTOPBIT;
    dcb.Parity = NOPARITY;
    dcb.fDtrControl = DTR_CONTROL_DISABLE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;
    SetCommState(h, &dcb);

    COMMTIMEOUTS timeouts = {0};
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = TIMEOUT_MS;
    timeouts.ReadTotalTimeoutMultiplier = 10;
    SetCommTimeouts(h, &timeouts);

    return h;
}

void serial_close(serial_t h) {
    CloseHandle(h);
}

int serial_write(serial_t h, const uint8_t* data, size_t len) {
    DWORD written;
    if (!WriteFile(h, data, (DWORD)len, &written, NULL)) return -1;
    return written;
}

int serial_read(serial_t h, uint8_t* data, size_t len) {
    DWORD read;
    if (!ReadFile(h, data, (DWORD)len, &read, NULL)) return -1;
    return read;
}

void serial_flush(serial_t h) {
    PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);
}

void list_serial_ports(void) {
    printf("Available serial ports:\n");
    for (int i = 1; i < 256; i++) {
        char port[16];
        snprintf(port, sizeof(port), "COM%d", i);
        HANDLE h = CreateFileA(port, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                              OPEN_EXISTING, 0, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            printf("  %s\n", port);
            CloseHandle(h);
        }
    }
}

#else  // Unix (Mac/Linux)

serial_t serial_open(const char* port, int baud) {
    int fd = open(port, O_RDWR | O_NOCTTY);
    if (fd == -1) return INVALID_SERIAL;

    struct termios options;
    tcgetattr(fd, &options);

    // Set baud rate - use standard rates for initial setup
    speed_t speed;
    switch(baud) {
        case 9600:    speed = B9600; break;
        case 19200:   speed = B19200; break;
        case 38400:   speed = B38400; break;
        case 57600:   speed = B57600; break;
        case 115200:  speed = B115200; break;
        #ifndef __APPLE__
        case 1000000: speed = B1000000; break;
        #endif
        default:
            #ifdef __APPLE__
                speed = B115200;  // macOS: use standard rate, will set custom later
            #else
                speed = B1000000;  // Linux: Default to 1 Mbaud
            #endif
            break;
    }
    cfsetispeed(&options, speed);
    cfsetospeed(&options, speed);

    options.c_cflag |= (CLOCAL | CREAD);
    options.c_cflag &= ~PARENB;
    options.c_cflag &= ~CSTOPB;
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;
    options.c_cflag &= ~CRTSCTS;

    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    options.c_iflag &= ~(IXON | IXOFF | IXANY);
    options.c_oflag &= ~OPOST;

    options.c_cc[VMIN] = 0;
    options.c_cc[VTIME] = TIMEOUT_MS / 100;

    tcsetattr(fd, TCSANOW, &options);

    #ifdef __APPLE__
        // macOS: Set custom baud rate using ioctl IOSSIOSPEED
        // This is required for non-standard rates like 1000000
        #define IOSSIOSPEED _IOW('T', 2, speed_t)
        speed_t custom_speed = baud;
        if (ioctl(fd, IOSSIOSPEED, &custom_speed) == -1) {
            close(fd);
            return INVALID_SERIAL;
        }
    #endif

    tcflush(fd, TCIOFLUSH);

    return fd;
}

void serial_close(serial_t fd) {
    close(fd);
}

int serial_write(serial_t fd, const uint8_t* data, size_t len) {
    return write(fd, data, len);
}

int serial_read(serial_t fd, uint8_t* data, size_t len) {
    return read(fd, data, len);
}

void serial_flush(serial_t fd) {
    tcflush(fd, TCIOFLUSH);
}

void list_serial_ports(void) {
    printf("Available serial ports:\n");

#ifdef __APPLE__
    // macOS: Look for /dev/cu.* devices
    DIR* dir = opendir("/dev");
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL) {
            if (strncmp(entry->d_name, "cu.", 3) == 0) {
                printf("  /dev/%s\n", entry->d_name);
            }
        }
        closedir(dir);
    }
#else
    // Linux: Look for /dev/ttyUSB*, /dev/ttyACM*, /dev/ttyS*
    DIR* dir = opendir("/dev");
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL) {
            if (strncmp(entry->d_name, "ttyUSB", 6) == 0 ||
                strncmp(entry->d_name, "ttyACM", 6) == 0 ||
                strncmp(entry->d_name, "ttyS", 4) == 0) {
                printf("  /dev/%s\n", entry->d_name);
            }
        }
        closedir(dir);
    }
#endif
}

#endif

// Upload protocol
bool send_byte(serial_t s, uint8_t byte, bool verbose) {
    if (serial_write(s, &byte, 1) != 1) return false;
    #ifdef _WIN32
        FlushFileBuffers(s);
    #else
        tcdrain(s);  // Wait for output to be transmitted (like Python's flush())
    #endif
    if (verbose) {
        printf("TX: 0x%02X ('%c')\n", byte, (byte >= 32 && byte < 127) ? byte : '.');
    }
    return true;
}

bool wait_for_ack(serial_t s, uint8_t expected_ack, bool verbose) {
    uint8_t response;
    int ret = serial_read(s, &response, 1);

    if (ret <= 0) {
        if (verbose) printf("ERROR: Timeout waiting for ACK\n");
        return false;
    }

    if (verbose) {
        printf("RX: 0x%02X ('%c') - Expected: 0x%02X ('%c')\n",
               response, (response >= 32 && response < 127) ? response : '.',
               expected_ack, expected_ack);
    }

    // Check expected ACK FIRST (like Python version), then check for NAK
    if (response == expected_ack) {
        return true;
    }

    if (response == 'N') {
        printf(COLOR_RED "ERROR: Received NAK" COLOR_RESET "\n");
        return false;
    }

    printf(COLOR_RED "ERROR: Wrong ACK - got 0x%02X, expected 0x%02X" COLOR_RESET "\n",
           response, expected_ack);
    return false;
}

bool upload_firmware(serial_t s, const uint8_t* data, size_t size, bool verbose) {
    init_crc32();

    progress_t prog = {
        .total_bytes = size + 5 + 5,  // Data + size + CRC
        .bytes_sent = 0,
        .start_time = get_time(),
        .verbose = verbose
    };

    uint32_t crc = calculate_crc32(data, size);

    if (!verbose) {
        printf("\n=== FAST Streaming Upload (NO chunking) ===\n");
        printf("Uploading firmware (%zu bytes, CRC: 0x%08X)...\n\n", size, crc);
    }

    // Step 1: Send 'upload' command
    if (verbose) printf("\n[1] Sending 'upload' command\n");
    const char* cmd = "upload\r";
    serial_write(s, (const uint8_t*)cmd, strlen(cmd));
#ifdef _WIN32
    Sleep(300);  // 300ms for shell to process (Windows uses milliseconds)
#else
    usleep(300000);  // 300ms for shell to process
#endif
    // Read and discard any echoed data (like Python version)
    int bytes_available = 0;
    #ifdef _WIN32
        COMSTAT stat;
        DWORD errors;
        if (ClearCommError(s, &errors, &stat)) {
            bytes_available = stat.cbInQue;
        }
    #else
        ioctl(s, FIONREAD, &bytes_available);
    #endif
    if (bytes_available > 0) {
        uint8_t discard[256];
        serial_read(s, discard, sizeof(discard));
        if (verbose) printf("Discarded %d bytes of echo\n", bytes_available);
    }

    // Step 2: Send 'R' (Ready)
    if (verbose) printf("\n[2] Ready Handshake\n");
    else printf("Handshake: ");
    if (!send_byte(s, 'R', verbose)) return false;
    if (!wait_for_ack(s, 'A', verbose)) return false;
    if (!verbose) printf("Ready... ");
    fflush(stdout);

    // Step 3: Send size
    if (verbose) printf("\n[3] Packet Size: %zu bytes\n", size);
    uint8_t size_bytes[4] = {
        size & 0xFF,
        (size >> 8) & 0xFF,
        (size >> 16) & 0xFF,
        (size >> 24) & 0xFF
    };
    if (serial_write(s, size_bytes, 4) != 4) return false;
    #ifdef _WIN32
        FlushFileBuffers(s);
    #else
        tcdrain(s);
    #endif
    if (verbose) {
        for (int i = 0; i < 4; i++) {
            printf("TX: 0x%02X ('%c')\n", size_bytes[i],
                   (size_bytes[i] >= 32 && size_bytes[i] < 127) ? size_bytes[i] : '.');
        }
    }
    if (!wait_for_ack(s, 'B', verbose)) return false;
    if (!verbose) printf("Size...\n");

    // Step 4: STREAM ALL DATA (no chunking, no ACKs!)
    // Send in 1024-byte blocks for progress bar updates
    if (verbose) {
        printf("\n[4] Streaming Data (NO chunking, NO ACKs)\n");
    } else {
        printf("Streaming data: %zu bytes...\n", size);
    }

    #define BLOCK_SIZE 1024
    size_t total_blocks = (size + BLOCK_SIZE - 1) / BLOCK_SIZE;  // Ceiling division
    size_t bytes_remaining = size;
    size_t offset = 0;

    // Reset progress tracking to show only data transfer
    prog.bytes_sent = 0;
    prog.total_bytes = size;
    prog.start_time = get_time();  // Reset timer for data transfer only

    for (size_t block = 0; block < total_blocks; block++) {
        size_t block_size = (bytes_remaining < BLOCK_SIZE) ? bytes_remaining : BLOCK_SIZE;

        if (serial_write(s, data + offset, block_size) != (int)block_size) {
            printf(COLOR_RED "\nERROR: Failed to send data block %zu/%zu" COLOR_RESET "\n",
                   block + 1, total_blocks);
            return false;
        }

        offset += block_size;
        bytes_remaining -= block_size;
        prog.bytes_sent += block_size;

        // Update progress bar after each block
        if (!verbose) {
            show_progress(&prog);
        }
    }

    // Flush after all data sent
    #ifdef _WIN32
        FlushFileBuffers(s);
    #else
        tcdrain(s);
    #endif

    if (!verbose) {
        show_progress(&prog);  // Final update to show 100%
        printf("\n");  // New line after progress bar
    }

    // Step 5: Send CRC
    if (verbose) printf("\n[5] CRC Verification: 0x%08X\n", crc);
    uint8_t crc_packet[5] = {
        'C',
        crc & 0xFF,
        (crc >> 8) & 0xFF,
        (crc >> 16) & 0xFF,
        (crc >> 24) & 0xFF
    };
    if (serial_write(s, crc_packet, 5) != 5) return false;
    #ifdef _WIN32
        FlushFileBuffers(s);
    #else
        tcdrain(s);
    #endif
    if (verbose) {
        for (int i = 0; i < 5; i++) {
            printf("TX: 0x%02X ('%c')\n", crc_packet[i],
                   (crc_packet[i] >= 32 && crc_packet[i] < 127) ? crc_packet[i] : '.');
        }
    }

    // Step 6: Wait for response (ACK 'C' + 4 CRC bytes)
    if (!verbose) printf("\nWaiting for FPGA CRC calculation...\n");
    uint8_t response[5];
    int total_read = 0;
    double timeout_start = get_time();
    while (total_read < 5) {
        int ret = serial_read(s, response + total_read, 5 - total_read);
        if (ret > 0) {
            total_read += ret;
        }
        // Check for overall timeout (5 seconds - enough for 512KB)
        if ((get_time() - timeout_start) > 5.0) {
            printf(COLOR_RED "\nERROR: Timeout waiting for CRC response (5s)" COLOR_RESET "\n");
            printf("Received %d of 5 bytes. FPGA may be stuck or calculating CRC.\n", total_read);
            if (total_read > 0) {
                printf("Partial response: ");
                for (int i = 0; i < total_read; i++) {
                    printf("0x%02X ", response[i]);
                }
                printf("\n");
            }
            return false;
        }
    }

    uint32_t fpga_crc = response[1] | (response[2] << 8) |
                        (response[3] << 16) | (response[4] << 24);

    if (!verbose) printf("\n");

    if (verbose) {
        printf("\nResponse: '%c' (0x%02X)\n", response[0], response[0]);
    }
    printf("FPGA CRC:     0x%08X\n", fpga_crc);
    printf("Expected CRC: 0x%08X\n", crc);

    if (response[0] == 'C' && fpga_crc == crc) {
        printf(COLOR_GREEN "%s SUCCESS - CRC Match!" COLOR_RESET "\n", CHECK_MARK);
        return true;
    } else {
        printf(COLOR_RED "%s FAILURE" COLOR_RESET "\n", CROSS_MARK);
        if (response[0] != 'C') {
            printf("  Wrong ACK: got '%c', expected 'C'\n", response[0]);
        }
        if (fpga_crc != crc) {
            printf("  CRC Mismatch: XOR=0x%08X\n", fpga_crc ^ crc);
        }
        return false;
    }
}

// Main
void print_usage(const char* prog) {
    printf("FAST Streaming Firmware Uploader - NO Chunking (%s)\n\n", PLATFORM);
    printf("Usage: %s [options] <firmware.bin>\n\n", prog);
    printf("Options:\n");
    printf("  -p, --port <port>     Serial port (required)\n");
    printf("  -b, --baud <rate>     Baud rate (default: %d)\n", DEFAULT_BAUD);
    printf("  -v, --verbose         Verbose output (show protocol details)\n");
    printf("  -l, --list            List available serial ports\n");
    printf("  -h, --help            Show this help\n\n");
    printf("NOTE: Use with hexedit_fast.bin on FPGA (not regular hexedit.bin)\n\n");
    printf("Examples:\n");
#ifdef _WIN32
    printf("  %s -p COM8 firmware.bin\n", prog);
    printf("  %s --list\n", prog);
#else
    printf("  %s -p /dev/cu.usbserial-XXXXX firmware.bin\n", prog);
    printf("  %s --list\n", prog);
#endif
}

int main(int argc, char** argv) {
    const char* port = NULL;
    const char* firmware = NULL;
    int baud = DEFAULT_BAUD;
    bool verbose = false;
    bool list_ports = false;

    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--port") == 0) {
            if (++i >= argc) { print_usage(argv[0]); return 1; }
            port = argv[i];
        } else if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--baud") == 0) {
            if (++i >= argc) { print_usage(argv[0]); return 1; }
            baud = atoi(argv[i]);
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose = true;
        } else if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--list") == 0) {
            list_ports = true;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            firmware = argv[i];
        }
    }

    if (list_ports) {
        list_serial_ports();
        return 0;
    }

    if (!port || !firmware) {
        print_usage(argv[0]);
        return 1;
    }

    // Read firmware file
    FILE* f = fopen(firmware, "rb");
    if (!f) {
        printf(COLOR_RED "ERROR: Cannot open %s" COLOR_RESET "\n", firmware);
        return 1;
    }

    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size > MAX_PACKET_SIZE) {
        printf(COLOR_RED "ERROR: Firmware too large (%zu bytes, max %d)" COLOR_RESET "\n",
               size, MAX_PACKET_SIZE);
        fclose(f);
        return 1;
    }

    uint8_t* data = malloc(size);
    if (!data) {
        printf(COLOR_RED "ERROR: Out of memory" COLOR_RESET "\n");
        fclose(f);
        return 1;
    }

    if (fread(data, 1, size, f) != size) {
        printf(COLOR_RED "ERROR: Failed to read firmware" COLOR_RESET "\n");
        fclose(f);
        free(data);
        return 1;
    }
    fclose(f);

    // Open serial port
    printf("Connecting to %s at %d baud...\n", port, baud);
    serial_t s = serial_open(port, baud);
    if (s == INVALID_SERIAL) {
        printf(COLOR_RED "ERROR: Cannot open %s" COLOR_RESET "\n", port);
        free(data);
        return 1;
    }

    printf(COLOR_GREEN "Connected." COLOR_RESET "\n");

    // Upload
    bool success = upload_firmware(s, data, size, verbose);

    // Cleanup
    serial_close(s);
    free(data);

    return success ? 0 : 1;
}
