/*
 * sysdep1.c	system dependent routines.
 *
 *		m_dtrtoggle	- drop dtr and raise it again
 *		m_break		- send BREAK signal
 *		m_getdcd	- get modem dcd status
 *		m_setdcd	- set modem dcd status
 *		m_savestate	- save modem state
 *		m_restorestate	- restore saved modem state
 *		m_nohang	- tell driver not to hang up at DTR drop
 *		m_hupcl		- set hangup on close on/off
 *		m_setparms	- set speed, parity, bits and stopbits
 *		m_readchk	- see if there is input waiting.
 *		m_wait		- wait for child to finish. Sysdep. too.
 *
 *		If it's possible, Posix termios are preferred.
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
 *   jl  23.06.97  adjustable DTR downtime
 */
#include <config.h>

#include "port.h"
#include "sysdep.h"
#include "minicom.h"

#ifdef __APPLE__
#include <IOKit/serial/ioss.h>
#endif

/*
 * This is for supporting higher baud rates on MacOS.
 * Reference:
 * https://github.com/npat-efault/picocom/blob/master/custbaud_bsd.h */
#ifndef B460800
#define B460800   460800
#endif
#ifndef B500000
#define B500000   500000
#endif
#ifndef B576000
#define B576000   576000
#endif
#ifndef B921600
#define B921600   921600
#endif
#ifndef B1000000
#define B1000000 1000000
#endif
#ifndef B1152000
#define B1152000 1152000
#endif
#ifndef B1500000
#define B1500000 1500000
#endif
#ifndef B2000000
#define B2000000 2000000
#endif
#ifndef B2500000
#define B2500000 2500000
#endif
#ifndef B3000000
#define B3000000 3000000
#endif
#ifndef B3500000
#define B3500000 3500000
#endif
#ifndef B4000000
#define B4000000 4000000
#endif

/* On MacOS, setting unusual baudrates (see list above) does not work
 * through tcsetattr but requires an additional ioctl. Interestingly,
 * tcgetattr then returns the right baud rate, so we do not need to do
 * anything there. */
static void set_speed_apple(int fd, struct termios *p)
{
#ifdef __APPLE__
  speed_t v = cfgetospeed(p);
  ioctl(fd, IOSSIOSPEED, &v);
#else
  (void)fd;
  (void)p;
#endif
}

/* Set hardware flow control. */
void m_sethwf(int fd, int on)
{
#ifdef POSIX_TERMIOS
  struct termios tty;
#endif

  if (portfd_is_socket)
	return;

#ifdef POSIX_TERMIOS
  tcgetattr(fd, &tty);
  if (on)
    tty.c_cflag |= CRTSCTS;
  else
    tty.c_cflag &= ~CRTSCTS;
  tcsetattr(fd, TCSANOW, &tty);
  set_speed_apple(fd, &tty);
#endif
}

/* Set RTS line. Sometimes dropped. Linux specific? */
static void m_setrts(int fd)
{
  if (portfd_is_socket)
    return;

#if defined(TIOCM_RTS) && defined(TIOCMODG)
  {
    int mcs=0;

    ioctl(fd, TIOCMODG, &mcs);
    mcs |= TIOCM_RTS;
    ioctl(fd, TIOCMODS, &mcs);
  }
#endif
}

/*
 * Drop DTR line and raise it again.
 */
void m_dtrtoggle(int fd, int sec)
{
  if (portfd_is_socket)
    return;

  {
#ifdef TIOCSDTR
    /* Use the ioctls meant for this type of thing. */
    ioctl(fd, TIOCCDTR, 0);
    sleep(sec);
    ioctl(fd, TIOCSDTR, 0);
#elif defined (TIOCMBIS) && defined (TIOCMBIC) && defined (TIOCM_DTR)
    int modembits = TIOCM_DTR;
    ioctl(fd, TIOCMBIC, &modembits);
    sleep (sec);
    ioctl(fd, TIOCMBIS, &modembits);
#else /* TIOCSDTR */
#  if defined (POSIX_TERMIOS)

    /* Posix - set baudrate to 0 and back */
    struct termios tty, old;

    tcgetattr(fd, &tty);
    tcgetattr(fd, &old);
    cfsetospeed(&tty, B0);
    cfsetispeed(&tty, B0);
    tcsetattr(fd, TCSANOW, &tty);
    sleep(sec);
    tcsetattr(fd, TCSANOW, &old);
    set_speed_apple(fd, &old);

#  else /* POSIX */
#    ifdef _V7

    /* Just drop speed to 0 and back to normal again */
    struct sgttyb sg, ng;

    ioctl(fd, TIOCGETP, &sg);
    ioctl(fd, TIOCGETP, &ng);

    ng.sg_ispeed = ng.sg_ospeed = 0;
    ioctl(fd, TIOCSETP, &ng);
    sleep(sec);
    ioctl(fd, TIOCSETP, &sg);

#    endif /* _V7 */
#  endif /* POSIX */
#endif /* TIOCSDTR */
  }
}

/*
 * Send a break
 */
void m_break(int fd)
{
  if (portfd_is_socket)
    return;

#ifdef POSIX_TERMIOS
  tcsendbreak(fd, 0);
#else
#  ifdef _V7
#    ifndef TIOCSBRK
  {
    struct sgttyb sg, ng;

    ioctl(fd, TIOCGETP, &sg);
    ioctl(fd, TIOCGETP, &ng);
    ng.sg_ispeed = ng.sg_ospeed = B110;
    ng.sg_flags = BITS8 | RAW;
    ioctl(fd, TIOCSETP, &ng);
    write(fd, "\0\0\0\0\0\0\0\0\0\0", 10);
    ioctl(fd, TIOCSETP, &sg);
  }
#    else
  ioctl(fd, TIOCSBRK, 0);
  sleep(1);
  ioctl(fd, TIOCCBRK, 0);
#    endif
#  endif
#endif
}

/*
 * Get the dcd status
 */
int m_getdcd(int fd)
{
  if (portfd_is_socket) {
    if (portfd_is_connected)
      return 1;
    /* we are not connected so this may be a good point to try to connect */
    term_socket_connect();
    return portfd_is_connected;
  }

#ifdef TIOCMODG
  {
    int mcs = 0;

    if (ioctl(fd, TIOCMODG, &mcs) < 0)
      return -1;
    return mcs & TIOCM_CAR ? 1 : 0;
  }
#else
  (void)fd;
  return 0; /* Impossible!! (eg. Coherent) */
#endif
}

/* Variables to save states in */
#ifdef POSIX_TERMIOS
static struct termios savetty;
static int m_word;
#else
#  if defined (_BSD43) || defined (_V7)
static struct sgttyb sg;
static struct tchars tch;
static int lsw;
static int m_word;
#  endif
#endif

/*
 * Save the state of a port
 */
void m_savestate(int fd)
{
  if (portfd_is_socket)
    return;

#ifdef POSIX_TERMIOS
  tcgetattr(fd, &savetty);
#else
#  if defined(_BSD43) || defined(_V7)
  ioctl(fd, TIOCGETP, &sg);
  ioctl(fd, TIOCGETC, &tch);
#  endif
#  ifdef _BSD43
  ioctl(fd, TIOCLGET, &lsw);
#  endif
#endif
#ifdef TIOCMODG
  ioctl(fd, TIOCMODG, &m_word);
#endif
}

/*
 * Restore the state of a port
 */
void m_restorestate(int fd)
{
  if (portfd_is_socket)
    return;

#ifdef POSIX_TERMIOS
  tcsetattr(fd, TCSANOW, &savetty);
  set_speed_apple(fd, &savetty);
#else
#  if defined(_BSD43) || defined(_V7)
  ioctl(fd, TIOCSETP, &sg);
  ioctl(fd, TIOCSETC, &tch);
#  endif
#  ifdef _BSD43
  ioctl(fd, TIOCLSET, &lsw);
#  endif
#endif
#ifdef TIOCMODS
  ioctl(fd, TIOCMODS, &m_word);
#endif
}

/*
 * Set the line status so that it will not kill our process
 * if the line hangs up.
 */
/*ARGSUSED*/
void m_nohang(int fd)
{
  if (portfd_is_socket)
    return;

  {
#ifdef POSIX_TERMIOS
    struct termios sgg;

    tcgetattr(fd, &sgg);
    sgg.c_cflag |= CLOCAL;
    tcsetattr(fd, TCSANOW, &sgg);
    set_speed_apple(fd, &sgg);
#else
#  if defined (_BSD43) && defined(LNOHANG)
    int lsw;

    ioctl(fd, TIOCLGET, &lsw);
    lsw |= LNOHANG;
    ioctl(fd, TIOCLSET, &lsw);
#  endif
#endif
  }
}

/*
 * Set hangup on close on/off.
 */
void m_hupcl(int fd, int on)
{
  if (portfd_is_socket)
    return;

  /* Eh, I don't know how to do this under BSD (yet..) */
#ifdef POSIX_TERMIOS
  {
    struct termios sgg;

    tcgetattr(fd, &sgg);
    if (on)
      sgg.c_cflag |= HUPCL;
    else
      sgg.c_cflag &= ~HUPCL;
    tcsetattr(fd, TCSANOW, &sgg);
    set_speed_apple(fd, &sgg);
  }
#endif
}

/*
 * See if there is input waiting.
 * returns: 0=nothing, >0=something, -1=can't.
 */
int m_readchk(int fd)
{
#ifdef FIONREAD
  long i = -1;

  ioctl(fd, FIONREAD, &i);
  return (int)i;
#else
#  if defined(F_GETFL) && defined(O_NDELAY)
  int i, old;
  char c;

  old = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, old | O_NDELAY);
  i = read(fd, &c, 1);
  fcntl(fd, F_SETFL, old);

  return i;
#  else
  return -1;
#  endif
#endif
}

/*
 * Get maximum speed.
 * Returns maximum speed in baud
 */
unsigned m_getmaxspd(void)
{
#if defined(B4000000)
  return 4000000;
#elif defined(B3500000)
  return 3500000;
#elif defined(B3000000)
  return 3000000;
#elif defined(B2500000)
  return 2500000;
#elif defined(B2000000)
  return 2000000;
#elif defined(B1500000)
  return 1500000;
#elif defined(B1152000)
  return 1152000;
#elif defined(B1000000)
  return 1000000;
#elif defined(B921600)
  return 921600;
#elif defined(B576000)
  return 576000;
#elif defined(B500000)
  return 500000;
#elif defined(B460800)
  return 460800;
#elif defined(B230400)
  return 230400;
#elif defined(B115200)
  return 115200;
#elif defined(B57600)
  return 57600;
#elif defined(B38400)
  return 38400;
#elif defined(EXTB)
  return 38400;
#elif defined(B19200)
  return 19200;
#elif defined(EXTA)
  return 19200;
#else
  return 9600;
#endif
}

/*
 * Set baudrate, parity and number of bits.
 */
void m_setparms(int fd, char *baudr, char *par, char *bits, char *stopb,
                int hwf, int swf, int rs485en)
{
  int spd = -1;
  int newbaud;
  int bit = bits[0];

#ifdef POSIX_TERMIOS
  struct termios tty;
#else /* POSIX_TERMIOS */
  struct sgttyb tty;
#endif /* POSIX_TERMIOS */

  if (portfd_is_socket)
    return;

#ifdef POSIX_TERMIOS
  tcgetattr(fd, &tty);
#else /* POSIX_TERMIOS */
  ioctl(fd, TIOCGETP, &tty);
#endif /* POSIX_TERMIOS */


  /* We generate mark and space parity ourselves. */
  if (bit == '7' && (par[0] == 'M' || par[0] == 'S'))
    bit = '8';

  /* Check if 'baudr' is really a number */
  if ((newbaud = (atol(baudr) / 100)) == 0 && baudr[0] != '0')
    newbaud = -1;

  switch (newbaud) {
    case 0:
#ifdef B0
      spd = B0;
#else
      spd = 0;
#endif
      break;
    case 3:	spd = B300;	break;
    case 6:	spd = B600;	break;
    case 12:	spd = B1200;	break;
    case 24:	spd = B2400;	break;
    case 48:	spd = B4800;	break;
    case 96:	spd = B9600;	break;
#ifdef B19200
    case 192:	spd = B19200;	break;
#else /* B19200 */
#  ifdef EXTA
    case 192:	spd = EXTA;	break;
#   else /* EXTA */
    case 192:	spd = B9600;	break;
#   endif /* EXTA */
#endif	 /* B19200 */
#ifdef B38400
    case 384:	spd = B38400;	break;
#else /* B38400 */
#  ifdef EXTB
    case 384:	spd = EXTB;	break;
#   else /* EXTB */
    case 384:	spd = B9600;	break;
#   endif /* EXTB */
#endif	 /* B38400 */
#ifdef B57600
    case 576:	spd = B57600;	break;
#endif
#ifdef B115200
    case 1152:	spd = B115200;	break;
#endif
#ifdef B230400
    case 2304:	spd = B230400;	break;
#endif
#ifdef B460800
    case 4608: spd = B460800; break;
#endif
#ifdef B500000
    case 5000: spd = B500000; break;
#endif
#ifdef B576000
    case 5760: spd = B576000; break;
#endif
#ifdef B921600
    case 9216: spd = B921600; break;
#endif
#ifdef B1000000
    case 10000: spd = B1000000; break;
#endif
#ifdef B1152000
    case 11520: spd = B1152000; break;
#endif
#ifdef B1500000
    case 15000: spd = B1500000; break;
#endif
#ifdef B2000000
    case 20000: spd = B2000000; break;
#endif
#ifdef B2500000
    case 25000: spd = B2500000; break;
#endif
#ifdef B3000000
    case 30000: spd = B3000000; break;
#endif
#ifdef B3500000
    case 35000: spd = B3500000; break;
#endif
#ifdef B4000000
    case 40000: spd = B4000000; break;
#endif
  }

#if defined (_BSD43) && !defined(POSIX_TERMIOS)
  if (spd != -1)
    tty.sg_ispeed = tty.sg_ospeed = spd;
  /* Number of bits is ignored */

  tty.sg_flags = RAW | TANDEM;
  if (par[0] == 'E')
    tty.sg_flags |= EVENP;
  else if (par[0] == 'O')
    tty.sg_flags |= ODDP;
  else
    tty.sg_flags |= PASS8 | ANYP;

  ioctl(fd, TIOCSETP, &tty);

#  ifdef TIOCSDTR
  /* FIXME: huh? - MvS */
  ioctl(fd, TIOCSDTR, 0);
#  endif
#endif /* _BSD43 && !POSIX_TERMIOS */

#if defined (_V7) && !defined(POSIX_TERMIOS)
  if (spd != -1)
    tty.sg_ispeed = tty.sg_ospeed = spd;
  tty.sg_flags = RAW;
  if (par[0] == 'E')
    tty.sg_flags |= EVENP;
  else if (par[0] == 'O')
    tty.sg_flags |= ODDP;

  ioctl(fd, TIOCSETP, &tty);
#endif /* _V7 && !POSIX */

#ifdef POSIX_TERMIOS

  if (spd != -1) {
    cfsetospeed(&tty, (speed_t)spd);
    cfsetispeed(&tty, (speed_t)spd);
  }

  switch (bit) {
    case '5':
      tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS5;
      break;
    case '6':
      tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS6;
      break;
    case '7':
      tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS7;
      break;
    case '8':
    default:
      tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
      break;
  }
  /* Set into raw, no echo mode */
  tty.c_iflag =  IGNBRK;
  tty.c_lflag = 0;
  tty.c_oflag = 0;
  tty.c_cflag |= CLOCAL | CREAD;
#ifdef _DCDFLOW
  tty.c_cflag &= ~CRTSCTS;
#endif
  tty.c_cc[VMIN] = 1;
  tty.c_cc[VTIME] = 5;

  if (swf)
    tty.c_iflag |= IXON | IXOFF;
  else
    tty.c_iflag &= ~(IXON|IXOFF|IXANY);

  tty.c_cflag &= ~(PARENB | PARODD);
  if (par[0] == 'E')
    tty.c_cflag |= PARENB;
  else if (par[0] == 'O')
    tty.c_cflag |= (PARENB | PARODD);

  if (stopb[0] == '2')
    tty.c_cflag |= CSTOPB;
  else
    tty.c_cflag &= ~CSTOPB;

  tcsetattr(fd, TCSANOW, &tty);
  set_speed_apple(fd, &tty);

  if (!rs485en)
    m_setrts(fd);
#endif /* POSIX_TERMIOS */

#ifndef _DCDFLOW
  m_sethwf(fd, hwf);
#endif
}

void m_set485parms(int fd, int en, int rts_on_snd, int rts_aft_snd,
                   int rx_dur_tx, int term_bus, char *del_rts_bef_snd,
                   char *del_rts_aft_snd)
{
#if defined (SER_RS485_ENABLED) && defined (TIOCGRS485) && defined (TIOCSRS485)
  struct serial_rs485 rs485conf;

  if (ioctl(fd, TIOCGRS485, &rs485conf))
	  return;

  if (en)
    rs485conf.flags |= SER_RS485_ENABLED;
  else
    rs485conf.flags &= ~SER_RS485_ENABLED;

  if (rts_on_snd)
    rs485conf.flags |= SER_RS485_RTS_ON_SEND;
  else
    rs485conf.flags &= ~SER_RS485_RTS_ON_SEND;

  if (rts_aft_snd)
    rs485conf.flags |= SER_RS485_RTS_AFTER_SEND;
  else
    rs485conf.flags &= ~SER_RS485_RTS_AFTER_SEND;

  rs485conf.delay_rts_before_send = atoi(del_rts_bef_snd);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36)
  rs485conf.delay_rts_after_send = atoi(del_rts_aft_snd);
#endif

#ifdef SER_RS485_RX_DURING_TX
  if (rx_dur_tx)
    rs485conf.flags |= SER_RS485_RX_DURING_TX;
  else
    rs485conf.flags &= ~SER_RS485_RX_DURING_TX;
#endif

#ifdef SER_RS485_TERMINATE_BUS
  if (term_bus)
    rs485conf.flags |= SER_RS485_TERMINATE_BUS;
  else
    rs485conf.flags &= ~SER_RS485_TERMINATE_BUS;
#endif

  ioctl(fd, TIOCSRS485, &rs485conf);
#else
  (void)fd;
  (void)en;
  (void)rts_on_snd;
  (void)rts_aft_snd;
  (void)rx_dur_tx;
  (void)term_bus;
  (void)del_rts_bef_snd;
  (void)del_rts_aft_snd;
#endif
}

/*
 * Wait for child and return pid + status
 */
int m_wait(int *stt)
{
#if defined (_BSD43) && !defined(POSIX_TERMIOS)
  int pid;
  union wait st1;

  pid = wait((void *)&st1);
  *stt = (unsigned)st1.w_retcode + 256 * (unsigned)st1.w_termsig;
  return pid;
#else
  int pid;
  int st1;

  pid = wait(&st1);
  *stt = (unsigned)WEXITSTATUS(st1) + 256 * (unsigned)WTERMSIG(st1);
  return pid;
#endif
}
