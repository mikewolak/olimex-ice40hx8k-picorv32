/*
 * main.c	main loop of emulator.
 *
 *		This file is part of the minicom communications package,
 *		Copyright 1991-1995 Miquel van Smoorenburg.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * fmg 1/11/94 color mods
 * jl  22.06.97 log it when DCD drops
 * jl  02.06.98 added call duration to the "Gone offline" log line
 * jl  14.06.99 moved lockfile creation to before serial port opening
 *
 */
#include <config.h>

#include "port.h"
#include "minicom.h"
#include "intl.h"
#include "sysdep.h"
#include <errno.h>

#include <stdbool.h>
#include <assert.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

static jmp_buf albuf;

static const char SOCKET_PREFIX_UNIX[] = "unix:";
static const char SOCKET_PREFIX_UNIX_LEGACY[] = "unix#";
static const char SOCKET_PREFIX_TCP[] = "tcp:";

/* Compile SCCS ID into executable. */
const char *Version = VERSION;

/*
 * Find out name to use for lockfile when locking tty.
 */
static char *mdevlockname(char *s, char *res, int reslen)
{
  char *p;

  if (strncmp(s, "/dev/", 5) == 0) {
    /* In /dev */
    strncpy(res, s + 5, reslen - 1);
    res[reslen - 1] = 0;
    for (p = res; *p; p++)
      if (*p == '/')
        *p = '_';
  } else {
    /* Outside of /dev. Do something sensible. */
    if ((p = strrchr(s, '/')) == NULL)
      p = s;
    else
      p++;
    strncpy(res, p, reslen - 1);
    res[reslen - 1] = 0;
  }

  return res;
}

static char *shortened_devpath(char *buf, int buflen, char *devpath)
{
  char *cutoff[] = {
    "/dev/serial/by-id/",
    "/dev/serial/by-path/",
    "/dev/serial/",
    "/dev/",
  };
  enum { SZ = sizeof(cutoff) / sizeof(cutoff[0]) };

  for (int i = 0; i < SZ; ++i)
    if (!strncmp(devpath, cutoff[i], strlen(cutoff[i])))
      {
        devpath += strlen(cutoff[i]);
        break;
      }

  int l = strlen(devpath);

  if (l > buflen - 1)
    devpath += l - buflen + 1;

  strncpy(buf, devpath, buflen);
  buf[buflen - 1] = 0;

  return buf;
}

/*
 * Leave.
 */
void leave(const char *s)
{
  if (stdwin)
    mc_wclose(stdwin, 1);
  if (portfd > 0)
    m_restorestate(portfd);

  device_close();

  if (P_CALLIN[0])
    fastsystem(P_CALLIN, NULL, NULL, NULL);
  fprintf(stderr, "%s", s);
  exit(1);
}

/*
 * Return text describing command-key.
 */
char *esc_key(void)
{
  static char buf[16];

  if (!alt_override && P_ESCAPE[0] == '^' && P_ESCAPE[1] != '[') {
    sprintf(buf, "CTRL-%c ", P_ESCAPE[1]);
    return buf;
  }
#if defined(i386) || defined(__i386__)
  sprintf(buf, "ESC,");
#else
  sprintf(buf, "Meta-");
#endif
  return buf;
}

static void get_alrm(int dummy)
{
  (void)dummy;
  errno = ETIMEDOUT;
  longjmp(albuf, 1);
}

static void term_socket_connect_unix(void)
{
  struct sockaddr_un sa_un;
  sa_un.sun_family = AF_UNIX;
  strncpy(sa_un.sun_path,
	  dial_tty + strlen(SOCKET_PREFIX_UNIX),
	  sizeof(sa_un.sun_path) - 1);
  sa_un.sun_path[sizeof(sa_un.sun_path) - 1] = 0;

  if ((portfd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
    return;

  if (connect(portfd, (struct sockaddr *)&sa_un, sizeof(sa_un)) == -1)
    term_socket_close();
  else
    portfd_is_connected = 1;
}

static void term_socket_connect_tcp(void)
{
  struct addrinfo hints;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family   = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  char *s = strdup(dial_tty);
  if (!s)
    return;

  char *s_addr = s + strlen(SOCKET_PREFIX_TCP);
  char *s_port = strchr(s_addr, ':');
  if (!s_port) {
    fprintf(stderr, "No port given\n");
    return;
  }
  *s_port = 0;
  s_port++;

  if (strlen(s_addr) == 0)
    s_addr = "localhost";

  struct addrinfo *result;
  int r = getaddrinfo(s_addr, s_port, &hints, &result);
  if (r) {
    fprintf(stderr, "Name resolution failed: %s\n", gai_strerror(r));
    return;
  }

  free(s);

  struct addrinfo *rp;
  for (rp = result; rp; rp = rp->ai_next) {
    portfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (portfd == -1)
      continue;

    if (connect(portfd, rp->ai_addr, rp->ai_addrlen) != -1) {
      portfd_is_connected = 1;
      break;
    }

    term_socket_close();
  }

  if (rp)
    freeaddrinfo(result);
}

/*
 * If portfd is a socket, we try to (re)connect
 */
void term_socket_connect(void)
{
  if (portfd_is_socket == Socket_type_no_socket || portfd_is_connected)
    return;

  if (portfd_is_socket == Socket_type_unix)
    term_socket_connect_unix();
  else if (portfd_is_socket == Socket_type_tcp)
    term_socket_connect_tcp();
}

/*
 * Close the connection if the socket delivers "eof" (read returns 0)
 */
void term_socket_close(void)
{
  close(portfd);
  portfd_is_connected = 0;
  portfd = -1;
}

static void lockfile_init()
{
#if HAVE_LOCKDEV
  lockfile_mode = Lockfile_mode_ttylock;
#else
  struct stat stt;
  char buf[128];

  if (P_LOCK[0] && stat(P_LOCK, &stt) == 0) {
    // Whether we can actually write the directory will be known on
    // lockfile-create time, then we switch to Lockfile_mode_flock
    lockfile_mode = Lockfile_mode_uucp;
    snprintf(lockfile, sizeof(lockfile),
                       "%s/LCK..%s",
                       P_LOCK, mdevlockname(dial_tty, buf, sizeof(buf)));

  }
  else
    lockfile_mode = Lockfile_mode_flock;
#endif
}

static bool check_lockfile(void)
{
#if HAVE_LOCKDEV
  return true;
#else
  if (lockfile_mode == Lockfile_mode_uucp) {
    struct stat statbuf;
    int r = stat(lockfile, &statbuf);
    if (r < 0 && errno != ENOENT) {
      if (stdwin)
        mc_wclose(stdwin, 1);
      fprintf(stderr, _("Lockfile %s cannot be queried (%d).\n"),
              lockfile, errno);
      return false;
    }

    if (r == 0 && statbuf.st_uid != getuid()) {
      if (stdwin)
        mc_wclose(stdwin, 1);
      fprintf(stderr, _("Lockfile %s owned by someone else (uid=%d).\n"),
              lockfile, statbuf.st_uid);
      return false;
    }

    int fd = open(lockfile, O_RDONLY);
    if (fd < 0) {
      if (errno == EACCES) { // Lockfile not accessible/readable
        if (stdwin)
          mc_wclose(stdwin, 1);
        fprintf(stderr, _("Device %s is locked by someone else.\n"),
                dial_tty);
        return false;
      }
    } else {

      union {
	char bytes[128];
	int kermit;
      } buf;

      int n = read(fd, buf.bytes, 127);
      close(fd);
      if (n > 0) {
        int pid = -1;
        if (n == 4)
          /* Kermit-style lockfile. */
          pid = buf.kermit;
        else {
          /* Ascii lockfile. */
          buf.bytes[n] = 0;
          sscanf(buf.bytes, "%d", &pid);
        }
        if (pid > 0 && kill((pid_t)pid, 0) < 0 &&
            errno == ESRCH) {
          fprintf(stderr, _("Lockfile is stale. Overriding it..\n"));
          sleep(1);
          unlink(lockfile);
        } else
          n = 0;
      }
      if (n == 0) {
        if (stdwin)
          mc_wclose(stdwin, 1);
        fprintf(stderr, _("Device %s is locked.\n"), dial_tty);
        return false;
      }
    }
  }

  return true;
#endif
}

static int device_open()
{
#if defined(O_NDELAY) && defined(F_SETFL)
  portfd = open(dial_tty, O_RDWR | O_NDELAY | O_NOCTTY);
  if (portfd < 0)
    return -1;

  /* Cancel the O_NDELAY flag. */
  int n = fcntl(portfd, F_GETFL, 0);
  fcntl(portfd, F_SETFL, n & ~O_NDELAY);
#else
  portfd = open(dial_tty, O_RDWR | O_NOCTTY);
  if (portfd < 0)
    return -1;
#endif

  // create after opening the dial_tty as we need a valid portfd
  return lockfile_create();
}

void device_close()
{
  if (portfd > 0)
    {
      lockfile_remove();
      close(portfd);
    }
  portfd = -1;
}


/*
 * Open the terminal.
 *
 * \return -1 on error, 0 on success
 */
int open_term(int doinit, int show_win_on_error, int no_msgs)
{
  portfd_is_socket = portfd_is_connected = 0;
  size_t ulen = strlen(SOCKET_PREFIX_UNIX);
  assert(ulen == strlen(SOCKET_PREFIX_UNIX_LEGACY));
  if (!strncmp(dial_tty, SOCKET_PREFIX_UNIX, ulen))
    portfd_is_socket = Socket_type_unix;
  else if (!strncmp(dial_tty, SOCKET_PREFIX_UNIX_LEGACY, ulen))
    portfd_is_socket = Socket_type_unix;
  else if (!strncmp(dial_tty, SOCKET_PREFIX_TCP, strlen(SOCKET_PREFIX_TCP)))
    portfd_is_socket = Socket_type_tcp;

  if (portfd_is_socket)
    goto nolock;

  lockfile_init();

  if (doinit > 0 && check_lockfile() == false)
    return -1;

nolock:
  /* Run a special program to disable callin if needed. */
  if (doinit > 0 && P_CALLOUT[0]) {
    if (fastsystem(P_CALLOUT, NULL, NULL, NULL) < 0) {
      if (stdwin)
        mc_wclose(stdwin, 1);
      fprintf(stderr, _("Could not setup for dial out.\n"));
      lockfile_remove();
      return -1;
    }
  }

  /* Now open the tty device. */
  if (setjmp(albuf) == 0) {
    portfd = -1;
    signal(SIGALRM, get_alrm);
    alarm(20);

    if (portfd_is_socket)
      term_socket_connect();
    else
      device_open();

    if (portfd < 0)
      {
	if (!no_msgs)
          fprintf(stderr, _("minicom: cannot open %s: %s\n"),
                          dial_tty, strerror(errno)); // errno retained from callers
        return -1;
      }

    if (doinit > 0)
      m_savestate(portfd);
    port_init();
  }

  int s_errno = errno;
  alarm(0);
  signal(SIGALRM, SIG_IGN);
  if (portfd < 0 && portfd_is_socket == Socket_type_no_socket) {
    if (!no_msgs) {
      if (doinit > 0) {
	if (stdwin)
	  mc_wclose(stdwin, 1);
	fprintf(stderr, _("minicom: cannot open %s: %s\n"),
			dial_tty, strerror(s_errno));
        lockfile_remove();
        return -1;
      }

      if (show_win_on_error)
	werror(_("Cannot open %s!"), dial_tty);
    }

    lockfile_remove();
    return -1;
  }

  /* Set CLOCAL mode */
  m_nohang(portfd);

  /* Set Hangup on Close if program crashes. (Hehe) */
  m_hupcl(portfd, 1);
  if (doinit > 0)
    m_flush(portfd);
  return 0;
}


/* Function to write output. */
static void do_output(const char *s, int len)
{
  char buf[256];

  if (len == 0)
    len = strlen(s);

  if (P_PARITY[0] == 'M') {
    int f;
    for (f = 0; f < len && f < (int)sizeof(buf); f++)
      buf[f] = *s++ | 0x80;
    len = f;
    s = buf;
  }

  int r;
  int b = vt_ch_delay ? 1 : len;
  while (len && (r = write(portfd, s, b)) >= 0)
    {
      s   += r;
      len -= r;
      if (vt_ch_delay)
        usleep(vt_ch_delay * 1000);
      else
        b = len;
    }
}

/* Function to handle keypad mode switches. */
static void kb_handler(int a, int b)
{
  cursormode = b;
  keypadmode = a;
  show_status();
}

/*
 * Initialize screen and status line.
 */
void init_emul(int type, int do_init)
{
  int x = -1, y = -1;
  char attr = 0;
  int maxy;
  int ypos;

  if (st) {
    mc_wclose(st, 1);
    tempst = 0;
    st = NULL;
  }

  if (us) {
    x = us->curx;
    y = us->cury;
    attr = us->attr;
    mc_wclose(us, 0);
  }

  /* See if we have space for a fixed status line */
  maxy = LINES - 1;
  if ((use_status || LINES > 24) &&
      P_STATLINE[0] == 'e') {
    if (use_status) {
      ypos = LINES;
      maxy = LINES - 1;
    } else {
      ypos = LINES - 1;
      maxy = LINES - 2;
    }
    st = mc_wopen(0, ypos, COLS - 1, ypos, BNONE,
               st_attr, sfcolor, sbcolor, 1, 0, 1);
    mc_wredraw(st, 1);
  }

  /* MARK updated 02/17/95 - Customizable size for history buffer */
  num_hist_lines = atoi(P_HISTSIZE);
  if (num_hist_lines < 0)
    num_hist_lines = 0;
  if (num_hist_lines > 5000)
    num_hist_lines = 5000;

  /* Open a new main window, and define the configured history buffer size. */
  us = mc_wopen(0, 0, COLS - 1, maxy,
              BNONE, XA_NORMAL, tfcolor, tbcolor, 1, num_hist_lines, 0);

  if (x >= 0) {
    mc_wlocate(us, x, y);
    mc_wsetattr(us, attr);
  }

  us->autocr = 0;
  us->wrap = wrapln;

  terminal = type;
  lines = LINES - (st != NULL);
  cols = COLS;

  /* Install and reset the terminal emulator. */
  if (do_init) {
    vt_install(do_output, kb_handler, us);
    vt_init(type, tfcolor, tbcolor, us->wrap, addlf, addcr);
  } else
    vt_pinit(us, -1, -1);

  show_status();
}

/*
 * Locate the cursor at the correct position in
 * the user screen.
 */
static void ret_csr(void)
{
  mc_wlocate(us, us->curx, us->cury);
  mc_wflush();
}


/**
 * Get status of device. It might happen that the device disappears (e.g.
 * due to a USB-serial unplug).
 *
 * \return 1 if device seems ok, 0 if it seems not available
 */
static int get_device_status(int fd)
{
  struct termios t;
  if (fd < 0)
    return 0;
  if (portfd_is_socket && portfd_is_connected)
    return 1;
  return !tcgetattr(fd, &t);
}


static char status_message[80];
static int  status_display_msg_until;
static int  status_message_showing;
static char *current_status_line;

static const char default_statusline_format[] = N_("%H for help | %b | %C | Minicom %V | %T | %t | %D");

static const char *statusline_format = default_statusline_format;

void set_status_line_format(const char *s)
{
  statusline_format = s;
}

/*
 * Show the status line
 */
static void show_status_fmt(const char *fmt)
{
  if (!st)
    return;

  char buf[COLS];
  int bufi = 0;
  int l = strlen(fmt);
  for (int i = 0; i < l && bufi < COLS; ++i)
    {
      if (fmt[i] == '%' && i + 1 < l)
        {
          char func = fmt[i + 1];
          ++i;

          switch (func)
            {
            case '%':
              bufi += scnprintf(buf + bufi, COLS - bufi, "%%");
              break;
            case 'H':
              bufi += scnprintf(buf + bufi, COLS - bufi, "%sZ", esc_key());
              break;
            case 'V':
              bufi += scnprintf(buf + bufi, COLS - bufi, "%s", VERSION);
              break;
            case 'b':
              if (portfd_is_socket == Socket_type_unix)
                bufi += scnprintf(buf + bufi, COLS - bufi, "unix-socket");
	      else if (portfd_is_socket == Socket_type_tcp)
                bufi += scnprintf(buf + bufi, COLS - bufi, "TCP");
              else
                {
                  if (P_SHOWSPD[0] == 'l')
                    bufi += scnprintf(buf + bufi, COLS - bufi, "%6ld", linespd);
                  else
                    bufi += scnprintf(buf + bufi, COLS - bufi, "%s", P_BAUDRATE);
                  bufi += scnprintf(buf + bufi, COLS - bufi, " %s%s%s",  P_BITS, P_PARITY, P_STOPB);
                }
              break;
            case 'T':
              switch (terminal)
                {
                case VT100:
                  bufi += scnprintf(buf + bufi, COLS - bufi, "VT102");
                  break;
                case ANSI:
                  bufi += scnprintf(buf + bufi, COLS - bufi, "ANSI");
                  break;
                }

              break;
            case 'C':
              bufi += scnprintf(buf + bufi, COLS - bufi, cursormode == NORMAL ? "NOR" : "APP");
              break;

	    case 't':
              if (online < 0)
                bufi += scnprintf(buf + bufi, COLS - bufi, "%s",
                                 P_HASDCD[0] == 'Y' ? _("Offline") : _("OFFLINE"));
              else
                bufi += scnprintf(buf + bufi, COLS - bufi, "%s %ld:%ld",
                                 P_HASDCD[0] == 'Y' ? _("Online") : _("ONLINE"),
                                 online / 3600, (online / 60) % 60);
              break;

            case 'D':
                {
                  char b[COLS - bufi];
                  bufi += scnprintf(buf + bufi, COLS - bufi, "%s",
                                   shortened_devpath(b, sizeof(b), dial_tty));
                }
              break;

            case '$':
              bufi += scnprintf(buf + bufi, COLS - bufi, "%s", status_message);
              break;

            default:
              bufi += scnprintf(buf + bufi, COLS - bufi, "?%c", func);
              break;
            }
        }
      else
        {
          buf[bufi] = fmt[i];
          bufi++;
        }
    }

  if (bufi < COLS - 1)
    memset(buf + bufi, ' ', COLS - bufi);
  buf[COLS - 1] = 0;

  if (size_changed || !current_status_line || strcmp(buf, current_status_line))
    {
      st->direct = 0;
      mc_wlocate(st, 0, 0);
      mc_wprintf(st, "%s", buf);
      mc_wredraw(st, 1);
      ret_csr();
      current_status_line = realloc(current_status_line, COLS);
      assert(current_status_line);
      strcpy(current_status_line, buf);
    }
}

void show_status()
{
  show_status_fmt(_(statusline_format));
}

time_t old_online = -2;

/*
 * Update the online time.
 */
static void update_status_time(void)
{
  time_t now;
  time(&now);

  if (status_message_showing)
    {
      if (now > status_display_msg_until)
        {
          /* time over for status message, restore standard status line */
          status_message_showing = 0;
          show_status();
        }
      else
        show_status_fmt("%$");
    }

  if (P_LOGCONN[0] == 'Y' && old_online >= 0 && online < 0)
    do_log(_("Gone offline (%ld:%02ld:%02ld)"),
           old_online / 3600, (old_online / 60) % 60, old_online % 60);

  old_online = online;

  if (!status_message_showing)
    show_status();
  mc_wflush();
}

void status_set_display(const char *text, int duration_s)
{
  time_t t;
  unsigned l;
  strncpy(status_message, text, sizeof(status_message));
  status_message[sizeof(status_message) - 1] = 0;
  l = strlen(status_message);
  for (; l < sizeof(status_message) - 1; ++l)
    status_message[l] = ' ';

  if (duration_s == 0)
    duration_s = 2;

  time(&t);
  status_display_msg_until = duration_s + t;
  status_message_showing = 1;
}

/* Update the timer display. This can also be called from updown.c */
void timer_update(void)
{
  static time_t t1, start;
  int dcd_support = portfd_is_socket || P_HASDCD[0] == 'Y';

  /* See if we're online. */
  if ((!dcd_support && bogus_dcd)
      || (dcd_support && m_getdcd(portfd) == 1)) {
    /* We are online at the moment. */
    if (online < 0) {
      /* This was a transition from off to online */
      time(&start);
      t1 = start;
      online = 0;
#ifdef _DCDFLOW
      /* DCD has gotten high, we can turn on hw flow control */
      if (P_HASRTS[0] == 'Y')
        m_sethwf(portfd, 1);
#endif
    }
  } else {
    /* We are offline at the moment. */
#ifdef _DCDFLOW
    if (online >= 0) {
      /* DCD has dropped, turn off hw flow control. */
      m_sethwf(portfd, 0);
    }
#endif
    if (online >= 0 && old_online >= 0) {
      /* First update the timer for call duration.. */
      time(&t1);
      online = t1 - start;
    }
    /* ..and THEN notify that we are now offline */
    online = -1;
  }

  /* Update online time */
  if (online >= 0) {
    time(&t1);
    online = t1 - start;
  }

  update_status_time();
}


/*
 * Show the name of the script running now.
 */
void scriptname(const char *s)
{
  if (st == NULL)
    return;
  mc_wlocate(st, 39, 0);
  if (*s == 0)
    mc_wprintf(st, "Minicom %-6.6s", VERSION);
  else
    mc_wprintf(st, "script %-7.7s", s);
  ret_csr();
}

/*
 * Show status line temporarily
 */
static void showtemp(void)
{
  if (st)
    return;

  st = mc_wopen(0, LINES - 1, COLS - 1, LINES - 1,
                BNONE, st_attr, sfcolor, sbcolor, 1, 0, 1);
  show_status();
  tempst = 1;
}

/*
 * The main terminal loop:
 *	- If there are characters received send them
 *	  to the screen via the appropriate translate function.
 */
int do_terminal(void)
{
  char buf[128];
  int buf_offset = 0;
  int c;
  int x;
  int blen;
  int zauto = 0;
  static const char zsig[] = "**\030B00";
  int zpos = 0;
  const char *s;
  dirflush = 0;
  WIN *error_on_open_window = NULL;

dirty_goto:
  /* Show off or online time */
  update_status_time();

  /* If the status line was shown temporarily, delete it again. */
  if (tempst) {
    tempst = 0;
    mc_wclose(st, 1);
    st = NULL;
  }


  /* Auto Zmodem? */
  if (P_PAUTO[0] >= 'A' && P_PAUTO[0] <= 'Z')
    zauto = P_PAUTO[0];
  /* Set the terminal modes */
  setcbreak(2); /* Raw, no echo */

  keyboard(KSTART, 0);

  /* Main loop */
  while (1) {
    /* See if window size changed */
    if (size_changed) {
      wrapln = us->wrap;
      /* I got the resize code going again! Yeah! */
      mc_wclose(us, 0);
      us = NULL;
      if (st)
        mc_wclose(st, 0);
      st = NULL;
      mc_wclose(stdwin, 0);
      if (win_init(tfcolor, tbcolor, XA_NORMAL) < 0)
        leave(_("Could not re-initialize window system."));
      /* Set the terminal modes */
      setcbreak(2); /* Raw, no echo */
      init_emul(terminal, 0);
      size_changed = 0;
    }
    /* Update the timer. */
    timer_update();

    /* check if device is ok, if not, try to open it */
    if (!get_device_status(portfd_connected())) {
      /* Ok, it's gone, most probably someone unplugged the USB-serial, we
       * need to free the FD so that a replug can get the same device
       * filename, open it again and be back */
      int reopen = portfd == -1;

      device_close();
      if (open_term(reopen, reopen, 1) < 0) {
        if (!error_on_open_window)
          error_on_open_window = mc_tell(_("Cannot open %s!"), dial_tty);
      } else {
        if (error_on_open_window) {
          mc_wclose(error_on_open_window, 1);
          error_on_open_window = NULL;
        }
      }
    }

    /* Check for I/O or timer. */
    x = check_io_frontend(buf + buf_offset, sizeof(buf) - buf_offset, &blen);
    blen += buf_offset;
    buf_offset = 0;

    /* Data from the modem to the screen. */
    if ((x & 1) == 1) {
      char obuf[sizeof(buf)];
      char *ptr;

      if (using_iconv()) {
        char *otmp = obuf;
        size_t output_len = sizeof(obuf);
        size_t input_len = blen;

        ptr = buf;
        do_iconv(&ptr, &input_len, &otmp, &output_len);

        // something happened at all?
        if (output_len < sizeof(obuf))
          {
            if (input_len)
              { // something remained, we need to adapt buf accordingly
                memmove(buf, ptr, input_len);
                buf_offset = input_len;
              }

            blen = sizeof(obuf) - output_len;
            ptr = obuf;
          }
	else
	  ptr = buf;
      } else {
        ptr = buf;
      }

      while (blen > 0) {
	int c = *ptr;
        /* Auto zmodem detect */
        if (zauto) {
          if (zsig[zpos] == c)
            zpos++;
          else
            zpos = 0;
        }
        if (P_PARITY[0] == 'M' || P_PARITY[0] == 'S')
          c &= 0x7f;
        if (display_hex) {
          unsigned char l = c;
          unsigned char u = l >> 4;
          l &= 0xf;
          vt_out(u > 9 ? 'a' + (u - 10) : '0' + u, 0);
          vt_out(l > 9 ? 'a' + (l - 10) : '0' + l, 0);
          vt_out(' ', 0);

          --blen;
          ++ptr;
        } else {
          wchar_t wc;
          size_t len = one_mbtowc(&wc, ptr, blen);
          vt_out(c, wc);
          blen -= len;
          ptr += len;
        }
        if (zauto && zsig[zpos] == 0) {
          dirflush = 1;
          keyboard(KSTOP, 0);
          updown('D', zauto - 'A');
          dirflush = 0;
          zpos = 0;
          blen = 0;
          goto dirty_goto;
        }
      }
      mc_wflush();
    }

    /* Read from the keyboard and send to modem. */
    if ((x & 2) == 2) {
      /* See which key was pressed. */
      c = keyboard(KGETKEY, 0);
      if (c == EOF)
        return EOF;

      if (c < 0) /* XXX - shouldn't happen */
        c += 256;

      /* Was this a command key? */
      if ((escape == 128 && c > 224 && c < 252) ||
          (escape != 27 && c == escape) ||
          (c > K_META)) {

        /* Stop keyserv process if we have it. */
        keyboard(KSTOP, 0);

        /* Show status line temporarily */
        showtemp();
        if (c == escape) /* CTRL A */
          c = keyboard(KGETKEY, 0);

        /* Restore keyboard modes */
        setcbreak(1); /* Cbreak, no echo */

        if (c > K_META)
          c -= K_META;
        if (c > 128)
          c -= 128;
        if (c > ' ') {
          dirflush = 1;
          m_flush(0);
          return c;
        }
        /* CTRLA - CTRLA means send one CTRLA */
#if 0
        write(portfd, &c, 1);
#else
        vt_send(c);
#endif
        goto dirty_goto;
      }

      /* No, just a key to be sent. */
      if (((c >= K_F1 && c <= K_F10) || c == K_F11 || c == K_F12)
	  && P_MACENAB[0] == 'Y') {
        s = "";
        switch(c) {
          case K_F1: s = P_MAC1; break;
          case K_F2: s = P_MAC2; break;
          case K_F3: s = P_MAC3; break;
          case K_F4: s = P_MAC4; break;
          case K_F5: s = P_MAC5; break;
          case K_F6: s = P_MAC6; break;
          case K_F7: s = P_MAC7; break;
          case K_F8: s = P_MAC8; break;
          case K_F9: s = P_MAC9; break;
          case K_F10: s = P_MAC10; break;
          case K_F11: s = P_MAC11; break;
          case K_F12: s = P_MAC12; break;
        }
        if (*s) {
	  while (*s)
	    vt_send(*s++);
	} else
          vt_send(c);
      } else
        vt_send(c);
    }
  }
}
