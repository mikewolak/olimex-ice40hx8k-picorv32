/*
 * fast-xfr.c   FAST streaming file transfer for minicom
 *
 *              Built-in FAST streaming protocol implementation.
 *              NO chunking, NO per-chunk ACKs, continuous streaming.
 *              SILENT MODE: NO debug output, ONLY serial port communication.
 *
 * Copyright (c) 2025 Michael Wolak
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <config.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <termios.h>
#include <stdint.h>

#include "port.h"
#include "minicom.h"
#include "intl.h"

/*
 * FAST Protocol Steps:
 * 1. Send 'R' (Ready command to start transfer)
 * 2. Wait for 'A' (Ready acknowledgment)
 * 3. Send size (4 bytes, little-endian)
 * 4. Wait for 'B' (Size acknowledged)
 * 5. Send CRC32 (4 bytes, little-endian)
 * 6. Stream ALL data continuously (NO chunking, NO ACKs)
 * 7. Wait for 'C' (Data received acknowledgment)
 * 8. Receive CRC32 from FPGA (4 bytes, little-endian)
 * 9. Compare CRCs
 *
 * IMPORTANT: This function writes ONLY to the serial port (fd).
 * NO debug output to stderr/stdout - completely silent operation.
 */

#define TIMEOUT_MS 4000  /* 4 seconds - prevent app hang on errors */

/* CRC32 calculation (0xEDB88320 polynomial) */
static uint32_t calculate_crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320;
            else
                crc >>= 1;
        }
    }
    return ~crc;
}

/* Wait for a character with timeout */
static int wait_for_char(int fd, char expected, int timeout_ms)
{
    struct timeval start, now;
    unsigned char c;
    int n;
    FILE *debug = fopen("/tmp/minicom-fast-debug.log", "a");

    gettimeofday(&start, NULL);

    while (1) {
        n = read(fd, &c, 1);
        if (n == 1) {
            if (debug) {
                fprintf(debug, "  Received char: 0x%02X ('%c') expecting 0x%02X ('%c')\n",
                        c, (c >= 32 && c < 127) ? c : '.', expected, expected);
                fflush(debug);
            }
            if (c == expected) {
                if (debug) fclose(debug);
                return 0;  /* Success */
            }
        }

        /* Check timeout */
        gettimeofday(&now, NULL);
        long elapsed_ms = (now.tv_sec - start.tv_sec) * 1000 +
                         (now.tv_usec - start.tv_usec) / 1000;
        if (elapsed_ms > timeout_ms) {
            if (debug) {
                fprintf(debug, "  Timeout after %ld ms\n", elapsed_ms);
                fclose(debug);
            }
            return -1;  /* Timeout */
        }

        usleep(1000);  /* Wait 1ms before retry */
    }
}

/* Send 4 bytes as little-endian */
static int send_uint32_le(int fd, uint32_t value)
{
    uint8_t buf[4];
    buf[0] = value & 0xFF;
    buf[1] = (value >> 8) & 0xFF;
    buf[2] = (value >> 16) & 0xFF;
    buf[3] = (value >> 24) & 0xFF;

    ssize_t written = write(fd, buf, 4);
    if (written != 4)
        return -1;
    return 0;
}

/* Read 4 bytes as little-endian with timeout */
static int read_uint32_le(int fd, uint32_t *value, int timeout_ms)
{
    uint8_t buf[4];
    size_t total = 0;
    struct timeval start, now;

    gettimeofday(&start, NULL);

    while (total < 4) {
        int n = read(fd, buf + total, 4 - total);
        if (n > 0)
            total += n;
        else
            usleep(1000);

        /* Check timeout */
        gettimeofday(&now, NULL);
        long elapsed_ms = (now.tv_sec - start.tv_sec) * 1000 +
                         (now.tv_usec - start.tv_usec) / 1000;
        if (elapsed_ms > timeout_ms) {
            return -1;  /* Timeout */
        }
    }

    *value = buf[0] | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24);
    return 0;  /* Success */
}

/*
 * FAST upload implementation
 * fd: serial port file descriptor (already configured by minicom)
 * filename: file to upload
 *
 * SILENT MODE: Writes ONLY to serial port, NO console output
 */
int fast_upload(int fd, const char *filename)
{
    FILE *fp;
    uint8_t *data = NULL;
    size_t file_size;
    uint32_t crc32;
    uint32_t fpga_crc32;
    int ret = -1;
    struct termios oldtio, newtio;
    int port_configured = 0;
    FILE *debug = fopen("/tmp/minicom-fast-debug.log", "a");

    if (debug) {
        fprintf(debug, "fast_upload() called with fd=%d, filename='%s'\n", fd, filename ? filename : "(null)");
        fflush(debug);
    }

    /* Save current port settings and configure for raw mode */
    tcgetattr(fd, &oldtio);
    port_configured = 1;
    newtio = oldtio;

    /* Raw mode - no processing */
    newtio.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    newtio.c_iflag &= ~(IXON | IXOFF | IXANY | ICRNL | INLCR);
    newtio.c_oflag &= ~OPOST;

    /* Read settings - return immediately */
    newtio.c_cc[VMIN] = 0;
    newtio.c_cc[VTIME] = 0;

    tcsetattr(fd, TCSANOW, &newtio);

    if (debug) {
        fprintf(debug, "Serial port configured for raw mode\n");
        fflush(debug);
    }

    /* Open file */
    fp = fopen(filename, "rb");
    if (!fp) {
        if (debug) {
            fprintf(debug, "ERROR: fopen failed for '%s': %s\n", filename, strerror(errno));
            fclose(debug);
        }
        return -1;
    }

    if (debug) {
        fprintf(debug, "File opened successfully\n");
        fflush(debug);
    }

    /* Get file size */
    fseek(fp, 0, SEEK_END);
    file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (debug) {
        fprintf(debug, "File size: %zu bytes\n", file_size);
        fflush(debug);
    }

    if (file_size == 0 || file_size > 524288) {
        if (debug) {
            fprintf(debug, "ERROR: Invalid file size %zu (must be 1-524288)\n", file_size);
            fclose(debug);
        }
        goto cleanup;
    }

    /* Read entire file */
    data = malloc(file_size);
    if (!data)
        goto cleanup;

    if (fread(data, 1, file_size, fp) != file_size)
        goto cleanup;

    /* Calculate CRC32 */
    crc32 = calculate_crc32(data, file_size);

    if (debug) {
        fprintf(debug, "CRC32 calculated: 0x%08X\n", crc32);
        fprintf(debug, "Starting protocol handshake...\n");
        fflush(debug);
    }

    /* Step 1: Send 'R' to signal transfer is starting */
    if (debug) {
        fprintf(debug, "Step 1: Sending 'R' to fd=%d\n", fd);
        fflush(debug);
    }

    ssize_t written = write(fd, "R", 1);
    if (debug) {
        fprintf(debug, "write() returned %zd\n", written);
        fflush(debug);
    }

    if (written != 1) {
        if (debug) {
            fprintf(debug, "ERROR: write('R') failed\n");
            fclose(debug);
        }
        goto cleanup;
    }

    /* Step 2: Wait for 'A' (Ready acknowledgment) */
    if (debug) {
        fprintf(debug, "Step 2: Waiting for 'A'...\n");
        fflush(debug);
    }

    int wait_result = wait_for_char(fd, 'A', TIMEOUT_MS);

    if (debug) {
        fprintf(debug, "wait_for_char returned: %d (0=success, -1=timeout)\n", wait_result);
        fflush(debug);
    }

    if (wait_result != 0) {
        if (debug) {
            fprintf(debug, "ERROR: Timeout waiting for 'A'\n");
            fclose(debug);
        }
        goto cleanup;
    }

    /* Step 3: Send size (4 bytes little-endian) */
    if (debug) {
        fprintf(debug, "Step 3: Sending size %zu (0x%08zX)\n", file_size, file_size);
        fflush(debug);
    }

    if (send_uint32_le(fd, file_size) != 0) {
        if (debug) {
            fprintf(debug, "ERROR: Failed to send size\n");
            fclose(debug);
        }
        goto cleanup;
    }

    /* Step 4: Wait for 'B' (Size acknowledgment) */
    if (debug) {
        fprintf(debug, "Step 4: Waiting for 'B'...\n");
        fflush(debug);
    }

    if (wait_for_char(fd, 'B', TIMEOUT_MS) != 0) {
        if (debug) {
            fprintf(debug, "ERROR: Timeout waiting for 'B'\n");
            fclose(debug);
        }
        goto cleanup;
    }

    /* Step 5: Stream ALL data continuously (no ACKs) */
    if (debug) {
        fprintf(debug, "Step 5: Streaming %zu bytes of data...\n", file_size);
        fflush(debug);
    }

    size_t sent = 0;
    struct timeval stream_start, stream_now;
    gettimeofday(&stream_start, NULL);

    while (sent < file_size) {
        size_t to_send = file_size - sent;
        if (to_send > 1024)
            to_send = 1024;

        ssize_t written = write(fd, data + sent, to_send);
        if (written <= 0) {
            if (debug) {
                fprintf(debug, "ERROR: write() failed at byte %zu, returned %zd\n", sent, written);
                fclose(debug);
            }
            goto cleanup;
        }

        sent += written;

        /* Check timeout - abort if streaming takes longer than timeout */
        gettimeofday(&stream_now, NULL);
        long elapsed_ms = (stream_now.tv_sec - stream_start.tv_sec) * 1000 +
                         (stream_now.tv_usec - stream_start.tv_usec) / 1000;
        if (elapsed_ms > TIMEOUT_MS) {
            if (debug) {
                fprintf(debug, "ERROR: Data streaming timeout after %zu bytes\n", sent);
                fclose(debug);
            }
            goto cleanup;
        }
    }

    if (debug) {
        fprintf(debug, "Step 5 complete: Sent %zu bytes\n", sent);
        fprintf(debug, "Draining output buffer...\n");
        fflush(debug);
    }

    /* Wait for all data to be physically transmitted */
    tcdrain(fd);

    if (debug) {
        fprintf(debug, "Output drained.\n");
        fprintf(debug, "Step 6: Sending 'C' + CRC32 (5 bytes total)\n");
        fprintf(debug, "CRC32: 0x%08X\n", crc32);
        fflush(debug);
    }

    /* Step 6: Send 'C' followed by CRC32 (5 bytes total) */
    uint8_t crc_packet[5];
    crc_packet[0] = 'C';
    crc_packet[1] = (crc32 >> 0) & 0xFF;
    crc_packet[2] = (crc32 >> 8) & 0xFF;
    crc_packet[3] = (crc32 >> 16) & 0xFF;
    crc_packet[4] = (crc32 >> 24) & 0xFF;

    if (write(fd, crc_packet, 5) != 5) {
        if (debug) {
            fprintf(debug, "ERROR: Failed to send 'C' + CRC32\n");
            fclose(debug);
        }
        goto cleanup;
    }

    tcdrain(fd);

    if (debug) {
        fprintf(debug, "Step 7: Waiting for FPGA response ('C' + calculated CRC)...\n");
        fflush(debug);
    }

    /* Give FPGA time to calculate CRC32 */
    usleep(100000);  /* 100ms */

    /* Step 7: Wait for 'C' from FPGA */
    if (wait_for_char(fd, 'C', TIMEOUT_MS) != 0) {
        if (debug) {
            fprintf(debug, "ERROR: Timeout waiting for 'C' response\n");
            fclose(debug);
        }
        goto cleanup;
    }

    /* Step 8: Receive FPGA's calculated CRC32 (4 bytes little-endian) */
    if (read_uint32_le(fd, &fpga_crc32, TIMEOUT_MS) != 0) {
        if (debug) {
            fprintf(debug, "ERROR: Timeout reading FPGA CRC32\n");
            fclose(debug);
        }
        goto cleanup;
    }

    if (debug) {
        fprintf(debug, "Received FPGA CRC32: 0x%08X\n", fpga_crc32);
        fprintf(debug, "Expected CRC32: 0x%08X\n", crc32);
        fflush(debug);
    }

    /* Step 9: Verify CRC match */
    if (fpga_crc32 == crc32)
        ret = 0;  /* Success */
    else
        ret = -1;  /* CRC mismatch */

cleanup:
    if (debug) {
        fprintf(debug, "Cleanup: ret=%d\n", ret);
        fclose(debug);
    }

    /* Restore original port settings */
    if (port_configured)
        tcsetattr(fd, TCSANOW, &oldtio);

    if (data)
        free(data);
    if (fp)
        fclose(fp);

    return ret;
}

/*
 * FAST download implementation
 * fd: serial port file descriptor
 * filename: file to save
 */
int fast_download(int fd, const char *filename)
{
    (void)fd;
    (void)filename;
    return -1;  /* Not implemented */
}
