/*
 * slattach - Attach serial lines as network interfaces (SLIP/CSLIP)
 *
 * Simplified version with high-speed baud rate support (up to 4 Mbaud)
 * Based on net-tools slattach 2.10
 *
 * Usage: slattach [-p protocol] [-s speed] [-L] [-d] tty
 *
 * Author: Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *         Modified for high-speed support by Michael Wolak, 2025
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <getopt.h>
#include <linux/if_slip.h>
#include <linux/serial.h>
#include <time.h>

#define VERSION "slattach 1M (high-speed SLIP - up to 4 Mbaud)"

/* Baud rate table - includes high-speed rates */
struct {
    const char *speed;
    int code;
} tty_speeds[] = {
    { "50",       B50      }, { "75",       B75      },
    { "110",      B110     }, { "300",      B300     },
    { "600",      B600     }, { "1200",     B1200    },
    { "2400",     B2400    }, { "4800",     B4800    },
    { "9600",     B9600    },
#ifdef B14400
    { "14400",    B14400   },
#endif
#ifdef B19200
    { "19200",    B19200   },
#endif
#ifdef B38400
    { "38400",    B38400   },
#endif
#ifdef B57600
    { "57600",    B57600   },
#endif
#ifdef B115200
    { "115200",   B115200  },
#endif
#ifdef B230400
    { "230400",   B230400  },
#endif
#ifdef B460800
    { "460800",   B460800  },
#endif
#ifdef B500000
    { "500000",   B500000  },
#endif
#ifdef B576000
    { "576000",   B576000  },
#endif
#ifdef B921600
    { "921600",   B921600  },
#endif
#ifdef B1000000
    { "1000000",  B1000000 },
#endif
#ifdef B1152000
    { "1152000",  B1152000 },
#endif
#ifdef B1500000
    { "1500000",  B1500000 },
#endif
#ifdef B2000000
    { "2000000",  B2000000 },
#endif
#ifdef B2500000
    { "2500000",  B2500000 },
#endif
#ifdef B3000000
    { "3000000",  B3000000 },
#endif
#ifdef B3500000
    { "3500000",  B3500000 },
#endif
#ifdef B4000000
    { "4000000",  B4000000 },
#endif
    { NULL,       0        }
};

/* Line disciplines */
#ifndef N_SLIP
#define N_SLIP 1
#endif
#ifndef N_CSLIP
#define N_CSLIP 2
#endif

/* Global state */
struct termios tty_saved, tty_current;
int tty_sdisc, tty_ldisc;
int tty_fd = -1;
int opt_L = 0;  /* 3-wire mode (no flow control) */
int opt_d = 0;  /* debug */
int opt_v = 1;  /* verbose (default ON) */
int opt_s = 1;  /* show statistics (default ON) */
int opt_q = 0;  /* quiet mode */

/* Statistics */
struct slip_stats {
    unsigned long rx_bytes;
    unsigned long tx_bytes;
    unsigned long rx_packets;
    unsigned long tx_packets;
    unsigned long rx_errors;
    unsigned long tx_errors;
} stats, last_stats;

static char interface_name[32] = {0};

/* Find baud rate code */
static int tty_find_speed(const char *speed) {
    int i = 0;
    while (tty_speeds[i].speed != NULL) {
        if (!strcmp(tty_speeds[i].speed, speed))
            return tty_speeds[i].code;
        i++;
    }
    return -1;
}

/* Set line speed */
static int tty_set_speed(struct termios *tty, const char *speed) {
    int code;

    if (opt_d) printf("Setting speed: %s\n", speed);

    code = tty_find_speed(speed);
    if (code < 0) {
        fprintf(stderr, "slattach: unknown speed: %s\n", speed);
        return -1;
    }

    cfsetispeed(tty, code);
    cfsetospeed(tty, code);
    return 0;
}

/* Put terminal in raw mode */
static int tty_set_raw(struct termios *tty) {
    int i;
    speed_t speed;

    /* Clear all special characters */
    for (i = 0; i < NCCS; i++)
        tty->c_cc[i] = '\0';

    tty->c_cc[VMIN] = 1;
    tty->c_cc[VTIME] = 0;

    /* Input: ignore breaks and parity errors */
    tty->c_iflag = (IGNBRK | IGNPAR);

    /* Output: raw */
    tty->c_oflag = 0;

    /* Local: raw */
    tty->c_lflag = 0;

    /* Save current speed */
    speed = cfgetospeed(tty);

    /* Control: 8N1, hang up on close, enable receiver */
    tty->c_cflag = (HUPCL | CREAD | CS8);

    if (opt_L)
        tty->c_cflag |= CLOCAL;     /* 3-wire mode */
    else
        tty->c_cflag |= CRTSCTS;    /* Hardware flow control */

    /* Restore speed */
    cfsetispeed(tty, speed);
    cfsetospeed(tty, speed);

    return 0;
}

/* Get terminal state */
static int tty_get_state(struct termios *tty) {
    if (tcgetattr(tty_fd, tty) < 0) {
        fprintf(stderr, "slattach: tcgetattr: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

/* Set terminal state */
static int tty_set_state(struct termios *tty) {
    if (tcsetattr(tty_fd, TCSANOW, tty) < 0) {
        fprintf(stderr, "slattach: tcsetattr: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

/* Get line discipline */
static int tty_get_disc(int *disc) {
    if (ioctl(tty_fd, TIOCGETD, disc) < 0) {
        fprintf(stderr, "slattach: TIOCGETD: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

/* Set line discipline */
static int tty_set_disc(int disc) {
    if (ioctl(tty_fd, TIOCSETD, &disc) < 0) {
        fprintf(stderr, "slattach: TIOCSETD(%d): %s\n", disc, strerror(errno));
        return -1;
    }
    return 0;
}

/* Get interface name */
static int tty_get_name(char *name) {
    if (ioctl(tty_fd, SIOCGIFNAME, name) < 0) {
        perror("slattach: SIOCGIFNAME");
        return -1;
    }
    return 0;
}

/* Hangup line */
static int tty_hangup(void) {
    struct termios tty;

    tty = tty_current;
    cfsetispeed(&tty, B0);
    cfsetospeed(&tty, B0);

    if (tty_set_state(&tty) < 0) {
        fprintf(stderr, "slattach: hangup failed: %s\n", strerror(errno));
        return -1;
    }

    sleep(1);

    if (tty_set_state(&tty_current) < 0) {
        fprintf(stderr, "slattach: restore failed: %s\n", strerror(errno));
        return -1;
    }

    return 0;
}

/* Close terminal */
static int tty_close(void) {
    tty_set_disc(tty_sdisc);
    tty_hangup();
    return 0;
}

/* Open and configure terminal */
static int tty_open(char *name, const char *speed) {
    char pathbuf[PATH_MAX];
    char *path;
    struct serial_struct serial;

    /* Build device path */
    if (name[0] != '/') {
        snprintf(pathbuf, sizeof(pathbuf), "/dev/%s", name);
        path = pathbuf;
    } else {
        path = name;
    }

    if (opt_d || opt_v) printf("Opening UART: %s\n", path);

    /* Open device */
    if ((tty_fd = open(path, O_RDWR | O_NDELAY)) < 0) {
        fprintf(stderr, "slattach: open(%s): %s\n", path, strerror(errno));
        return -1;
    }

    if (opt_d) printf("  Device opened: fd=%d\n", tty_fd);

    /* Get UART hardware info if available */
    if (opt_d && ioctl(tty_fd, TIOCGSERIAL, &serial) == 0) {
        printf("  UART Info:\n");
        printf("    Type: %d, Line: %d\n", serial.type, serial.line);
        printf("    Port: 0x%x, IRQ: %d\n", serial.port, serial.irq);
        printf("    Flags: 0x%x\n", serial.flags);
    }

    /* Get current state */
    if (tty_get_state(&tty_saved) < 0)
        return -1;

    tty_current = tty_saved;

    /* Get current line discipline */
    if (tty_get_disc(&tty_sdisc) < 0)
        return -1;

    tty_ldisc = tty_sdisc;

    /* Set raw mode */
    if (tty_set_raw(&tty_current) < 0) {
        fprintf(stderr, "slattach: cannot set raw mode\n");
        return -1;
    }

    /* Set speed if requested */
    if (speed != NULL) {
        if (tty_set_speed(&tty_current, speed) != 0) {
            fprintf(stderr, "slattach: cannot set speed %s\n", speed);
            return -1;
        }
    }

    /* Apply settings */
    if (tty_set_state(&tty_current) < 0)
        return -1;

    /* Display configuration */
    if (opt_d || opt_v) {
        printf("  UART Configuration:\n");
        printf("    Speed: %s baud\n", speed ? speed : "default");
        printf("    Mode: 8N1 (8 data bits, no parity, 1 stop bit)\n");
        printf("    Flow control: %s\n", opt_L ? "None (3-wire)" : "RTS/CTS");
        printf("    Raw mode: Yes (binary transparent)\n");
    }

    return 0;
}

/* Read interface statistics from /proc/net/dev */
static int read_interface_stats(const char *ifname, struct slip_stats *s) {
    FILE *fp;
    char line[256];
    int found = 0;

    memset(s, 0, sizeof(*s));

    fp = fopen("/proc/net/dev", "r");
    if (!fp) {
        return -1;
    }

    /* Skip header lines */
    if (fgets(line, sizeof(line), fp) == NULL) {
        fclose(fp);
        return -1;
    }
    if (fgets(line, sizeof(line), fp) == NULL) {
        fclose(fp);
        return -1;
    }

    /* Find our interface */
    while (fgets(line, sizeof(line), fp)) {
        char iface[32];
        unsigned long rx_bytes, rx_packets, rx_errs, rx_drop, rx_fifo, rx_frame, rx_compressed, rx_multicast;
        unsigned long tx_bytes, tx_packets, tx_errs, tx_drop, tx_fifo, tx_colls, tx_carrier, tx_compressed;

        if (sscanf(line, "%31[^:]:%lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu",
                   iface, &rx_bytes, &rx_packets, &rx_errs, &rx_drop, &rx_fifo, &rx_frame, &rx_compressed, &rx_multicast,
                   &tx_bytes, &tx_packets, &tx_errs, &tx_drop, &tx_fifo, &tx_colls, &tx_carrier, &tx_compressed) >= 17) {

            /* Remove leading spaces from interface name */
            char *p = iface;
            while (*p == ' ') p++;

            if (!strcmp(p, ifname)) {
                s->rx_bytes = rx_bytes;
                s->rx_packets = rx_packets;
                s->rx_errors = rx_errs;
                s->tx_bytes = tx_bytes;
                s->tx_packets = tx_packets;
                s->tx_errors = tx_errs;
                found = 1;
                break;
            }
        }
    }

    fclose(fp);
    return found ? 0 : -1;
}

/* Get remote IP address from interface */
static int get_remote_ip(const char *ifname, char *ip_buf, size_t buf_len) {
    FILE *fp;
    char line[256];
    char iface[32];

    fp = fopen("/proc/net/route", "r");
    if (!fp) {
        return -1;
    }

    /* Skip header */
    if (fgets(line, sizeof(line), fp) == NULL) {
        fclose(fp);
        return -1;
    }

    /* Find route for our interface */
    while (fgets(line, sizeof(line), fp)) {
        unsigned long dest_addr;
        int flags;

        if (sscanf(line, "%31s %lx %*x %x", iface, &dest_addr, &flags) >= 3) {
            if (strcmp(iface, ifname) == 0 && dest_addr != 0) {
                /* Found a route - convert to IP string */
                snprintf(ip_buf, buf_len, "%lu.%lu.%lu.%lu",
                        dest_addr & 0xFF,
                        (dest_addr >> 8) & 0xFF,
                        (dest_addr >> 16) & 0xFF,
                        (dest_addr >> 24) & 0xFF);
                fclose(fp);
                return 0;
            }
        }
    }

    fclose(fp);
    return -1;
}

/* Display statistics */
static void show_statistics(const char *ifname) {
    struct slip_stats current;
    static time_t last_time = 0;
    static int connection_detected = 0;
    time_t now = time(NULL);
    double elapsed;

    if (read_interface_stats(ifname, &current) < 0) {
        return;
    }

    if (last_time == 0) {
        /* First time - just save baseline */
        last_stats = current;
        last_time = now;
        printf("\n[Monitoring %s - press Ctrl-C to stop]\n\n", ifname);
        return;
    }

    /* Detect first incoming data (connection established) */
    if (!connection_detected && current.rx_packets > 0) {
        char remote_ip[32];
        connection_detected = 1;

        if (get_remote_ip(ifname, remote_ip, sizeof(remote_ip)) == 0) {
            printf("\n✓ Connection established with %s\n\n", remote_ip);
        } else {
            printf("\n✓ Connection active (receiving data)\n\n");
        }
    }

    elapsed = difftime(now, last_time);
    if (elapsed < 1.0) {
        return;
    }

    /* Calculate deltas */
    unsigned long rx_bytes_delta = current.rx_bytes - last_stats.rx_bytes;
    unsigned long tx_bytes_delta = current.tx_bytes - last_stats.tx_bytes;
    unsigned long rx_pkts_delta = current.rx_packets - last_stats.rx_packets;
    unsigned long tx_pkts_delta = current.tx_packets - last_stats.tx_packets;

    /* Calculate rates */
    double rx_rate = rx_bytes_delta / elapsed;
    double tx_rate = tx_bytes_delta / elapsed;

    /* Print statistics */
    printf("\r\033[K");  /* Clear line */
    printf("RX: %7lu pkts %8.1f KB/s | TX: %7lu pkts %8.1f KB/s | Total: RX %lu KB TX %lu KB",
           rx_pkts_delta,
           rx_rate / 1024.0,
           tx_pkts_delta,
           tx_rate / 1024.0,
           current.rx_bytes / 1024,
           current.tx_bytes / 1024);
    fflush(stdout);

    last_stats = current;
    last_time = now;
}

/* Signal handler */
static void sig_catch(int sig) {
    (void)sig;  /* Unused - same handler for all signals */
    printf("\n\nStopping SLIP interface...\n");
    tty_close();
    exit(0);
}

/* Usage */
static void usage(int rc) {
    FILE *fp = rc ? stderr : stdout;
    fprintf(fp, "Usage: slattach_1m [-p protocol] [-s speed] [-L] [-d] [-q] tty\n");
    fprintf(fp, "       slattach_1m -V (version)\n");
    fprintf(fp, "\n");
    fprintf(fp, "Options:\n");
    fprintf(fp, "  -p protocol  Protocol: slip, cslip (default: cslip)\n");
    fprintf(fp, "  -s speed     Baud rate (e.g., 115200, 1000000)\n");
    fprintf(fp, "  -L           3-wire mode (no flow control)\n");
    fprintf(fp, "  -d           Debug mode (show UART details)\n");
    fprintf(fp, "  -q, --quiet  Quiet mode (suppress statistics and verbose output)\n");
    fprintf(fp, "  -V           Show version\n");
    fprintf(fp, "\n");
    fprintf(fp, "Supported baud rates: 9600 - 4000000\n");
    fprintf(fp, "Recommended for FPGA: 1000000 (1 Mbaud)\n");
    fprintf(fp, "\n");
    fprintf(fp, "By default, shows connection status and live statistics.\n");
    fprintf(fp, "Use -q/--quiet to suppress output and run in background.\n");
    fprintf(fp, "\n");
    fprintf(fp, "Example:\n");
    fprintf(fp, "  sudo slattach_1m -p slip -s 1000000 -L /dev/ttyUSB0\n");
    fprintf(fp, "\n");
    fprintf(fp, "Quiet mode for scripts:\n");
    fprintf(fp, "  sudo slattach_1m -p slip -s 1000000 -L -q /dev/ttyUSB0 &\n");
    exit(rc);
}

/* Main */
int main(int argc, char *argv[]) {
    const char *proto = "cslip";
    const char *speed = NULL;
    char *tty_name;
    char ifname[128];
    int ldisc;
    int opt;

    /* Parse options */
    while ((opt = getopt(argc, argv, "p:s:LdqVh")) != -1) {
        switch (opt) {
            case 'p':
                proto = optarg;
                break;
            case 's':
                speed = optarg;
                break;
            case 'L':
                opt_L = 1;
                break;
            case 'd':
                opt_d = 1;
                break;
            case 'q':
                opt_q = 1;
                opt_v = 0;  /* Disable verbose */
                opt_s = 0;  /* Disable statistics */
                break;
            case 'V':
                printf("%s\n", VERSION);
                exit(0);
            case 'h':
            default:
                usage(1);
        }
    }

    /* Check for --quiet long option */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--quiet") == 0) {
            opt_q = 1;
            opt_v = 0;
            opt_s = 0;
        }
    }

    /* Get TTY name */
    if (optind != argc - 1)
        usage(1);

    tty_name = argv[optind];

    /* Open and configure TTY */
    if (tty_open(tty_name, speed) < 0)
        exit(1);

    /* Determine line discipline */
    if (!strcmp(proto, "slip")) {
        ldisc = N_SLIP;
    } else if (!strcmp(proto, "cslip")) {
        ldisc = N_CSLIP;
    } else {
        fprintf(stderr, "slattach: unknown protocol: %s\n", proto);
        exit(1);
    }

    /* Set line discipline */
    if (tty_set_disc(ldisc) < 0) {
        fprintf(stderr, "slattach: cannot set %s line discipline\n", proto);
        exit(1);
    }

    /* Get interface name */
    if (tty_get_name(ifname) == 0) {
        /* Save interface name for statistics */
        strncpy(interface_name, ifname, sizeof(interface_name) - 1);
        interface_name[sizeof(interface_name) - 1] = '\0';

        if (!opt_q) {
            printf("\n");
            printf("========================================\n");
            printf("SLIP Interface Ready\n");
            printf("========================================\n");
            printf("Protocol:   %s\n", proto);
            printf("Device:     %s\n", tty_name);
            printf("Interface:  %s\n", ifname);
            printf("Speed:      %s baud\n", speed ? speed : "default");
            printf("Mode:       %s\n", opt_L ? "3-wire (no flow control)" : "hardware flow control");
            printf("Statistics: %s\n", opt_s ? "enabled" : "disabled");
            printf("========================================\n");
            printf("\n");
            printf("Next steps:\n");
            printf("  sudo ifconfig %s 192.168.100.1 pointopoint 192.168.100.2 up\n", ifname);
            printf("  ping 192.168.100.2\n");
            printf("\n");
            if (!opt_s) {
                printf("Press Ctrl-C to stop...\n");
                printf("\n");
            }
        }
    }

    /* Install signal handlers */
    signal(SIGHUP, sig_catch);
    signal(SIGINT, sig_catch);
    signal(SIGQUIT, sig_catch);
    signal(SIGTERM, sig_catch);

    /* Main loop */
    while (1) {
        if (opt_s && interface_name[0]) {
            /* Show statistics every second */
            sleep(1);
            show_statistics(interface_name);
        } else {
            /* No statistics - just keep alive */
            sleep(60);
        }
    }

    return 0;
}
