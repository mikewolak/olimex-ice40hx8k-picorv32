/*
 * fast-xfr.c   FAST streaming file transfer for minicom
 *
 *              Built-in FAST streaming protocol implementation.
 *              NO chunking, NO per-chunk ACKs, continuous streaming.
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
 * 1. Wait for 'A' (Ready)
 * 2. Send size (4 bytes, little-endian)
 * 3. Wait for 'B' (Size acknowledged)
 * 4. Send CRC32 (4 bytes, little-endian)
 * 5. Stream ALL data continuously (NO chunking, NO ACKs)
 * 6. Wait for 'C' (CRC32 acknowledgment)
 * 7. Receive CRC32 from FPGA (4 bytes)
 * 8. Compare CRCs
 */

#define TIMEOUT_MS 5000

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

    gettimeofday(&start, NULL);

    while (1) {
        n = read(fd, &c, 1);
        if (n == 1 && c == expected)
            return 0;  /* Success */

        /* Check timeout */
        gettimeofday(&now, NULL);
        long elapsed_ms = (now.tv_sec - start.tv_sec) * 1000 +
                         (now.tv_usec - start.tv_usec) / 1000;
        if (elapsed_ms > timeout_ms)
            return -1;  /* Timeout */

        usleep(1000);  /* Wait 1ms before retry */
    }
}

/* Send 4 bytes as little-endian */
static void send_uint32_le(int fd, uint32_t value)
{
    uint8_t buf[4];
    buf[0] = value & 0xFF;
    buf[1] = (value >> 8) & 0xFF;
    buf[2] = (value >> 16) & 0xFF;
    buf[3] = (value >> 24) & 0xFF;
    write(fd, buf, 4);
}

/* Read 4 bytes as little-endian */
static uint32_t read_uint32_le(int fd)
{
    uint8_t buf[4];
    size_t total = 0;

    while (total < 4) {
        int n = read(fd, buf + total, 4 - total);
        if (n > 0)
            total += n;
        else
            usleep(1000);
    }

    return buf[0] | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24);
}

/* Progress bar display */
static void show_progress(size_t sent, size_t total, time_t start_time)
{
    time_t now = time(NULL);
    int elapsed = now - start_time;
    if (elapsed < 1) elapsed = 1;

    int percent = (sent * 100) / total;
    int bars = percent / 2;  /* 50 character bar */

    fprintf(stderr, "\r[");
    for (int i = 0; i < 50; i++) {
        if (i < bars)
            fprintf(stderr, "=");
        else if (i == bars)
            fprintf(stderr, ">");
        else
            fprintf(stderr, " ");
    }

    int speed = sent / elapsed;
    fprintf(stderr, "] %d%% | %zu/%zu bytes | %d KB/s | ETA: %.1fs",
            percent, sent, total, speed / 1024,
            (double)(total - sent) / speed);

    fflush(stderr);
}

/*
 * FAST upload implementation
 * fd: serial port file descriptor (already configured by minicom)
 * filename: file to upload
 */
int fast_upload(int fd, const char *filename)
{
    FILE *fp;
    uint8_t *data = NULL;
    size_t file_size;
    uint32_t crc32;
    uint32_t fpga_crc32;
    time_t start_time;
    int ret = -1;

    /* Open file */
    fp = fopen(filename, "rb");
    if (!fp) {
        fprintf(stderr, _("Error: Cannot open file '%s'\n"), filename);
        return -1;
    }

    /* Get file size */
    fseek(fp, 0, SEEK_END);
    file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (file_size == 0 || file_size > 524288) {
        fprintf(stderr, _("Error: File size %zu bytes (must be 1-524288)\n"),
                file_size);
        goto cleanup;
    }

    /* Read entire file */
    data = malloc(file_size);
    if (!data) {
        fprintf(stderr, _("Error: Out of memory\n"));
        goto cleanup;
    }

    if (fread(data, 1, file_size, fp) != file_size) {
        fprintf(stderr, _("Error: Failed to read file\n"));
        goto cleanup;
    }

    /* Calculate CRC32 */
    crc32 = calculate_crc32(data, file_size);

    fprintf(stderr, _("\n=== FAST Streaming Upload (NO chunking) ===\n"));
    fprintf(stderr, _("Uploading firmware (%zu bytes, CRC: 0x%08X)...\n\n"),
            file_size, crc32);

    /* Step 1: Wait for 'A' (Ready) */
    fprintf(stderr, _("Handshake: Ready... "));
    fflush(stderr);
    if (wait_for_char(fd, 'A', TIMEOUT_MS) != 0) {
        fprintf(stderr, _("TIMEOUT\n"));
        goto cleanup;
    }

    /* Step 2: Send size */
    send_uint32_le(fd, file_size);

    /* Step 3: Wait for 'B' */
    fprintf(stderr, _("Size... "));
    fflush(stderr);
    if (wait_for_char(fd, 'B', TIMEOUT_MS) != 0) {
        fprintf(stderr, _("TIMEOUT\n"));
        goto cleanup;
    }

    /* Step 4: Send CRC32 */
    send_uint32_le(fd, crc32);
    fprintf(stderr, _("OK\n"));

    /* Step 5: Stream ALL data in blocks for progress updates */
    fprintf(stderr, _("Streaming data: %zu bytes...\n"), file_size);
    start_time = time(NULL);

    size_t sent = 0;
    size_t block_size = 1024;  /* 1KB blocks for progress */

    while (sent < file_size) {
        size_t to_send = file_size - sent;
        if (to_send > block_size)
            to_send = block_size;

        ssize_t written = write(fd, data + sent, to_send);
        if (written > 0) {
            sent += written;
            show_progress(sent, file_size, start_time);
        } else {
            fprintf(stderr, _("\nError: Write failed\n"));
            goto cleanup;
        }
    }

    fprintf(stderr, _("\n\nWaiting for FPGA CRC calculation...\n"));

    /* Step 6: Wait for 'C' */
    if (wait_for_char(fd, 'C', TIMEOUT_MS) != 0) {
        fprintf(stderr, _("TIMEOUT waiting for CRC ACK\n"));
        goto cleanup;
    }

    /* Step 7: Receive FPGA CRC32 */
    fpga_crc32 = read_uint32_le(fd);

    /* Step 8: Verify */
    fprintf(stderr, _("FPGA CRC:     0x%08X\n"), fpga_crc32);
    fprintf(stderr, _("Expected CRC: 0x%08X\n"), crc32);

    if (fpga_crc32 == crc32) {
        fprintf(stderr, _("%s SUCCESS - CRC Match!\n"), "✓");
        ret = 0;
    } else {
        fprintf(stderr, _("%s FAILURE - CRC Mismatch!\n"), "✗");
    }

cleanup:
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
    fprintf(stderr, _("FAST download not yet implemented\n"));
    return -1;
}
