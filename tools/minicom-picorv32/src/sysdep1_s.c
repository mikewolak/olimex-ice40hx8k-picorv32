/*
 * sysdep1_s.c	system dependent routines.
 * 		sysdep1.c has a dependency to function not needed
 * 		for runscript so put m_flush (the only needed sysdep
 * 		function by runscript) into a separate object.
 *
 *		m_flush		- flush
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
 */
#include <config.h>

#include "sysdep.h"
#include "minicom.h"

enum Socket_type portfd_is_socket;
int portfd_is_connected;

/*
 * Flush the buffers
 */
void m_flush(int fd)
{
  if (portfd_is_socket)
    return;

/* Should I Posixify this, or not? */
#ifdef TCFLSH
  ioctl(fd, TCFLSH, 2);
#endif
#ifdef TIOCFLUSH
  {
    int out = 0;
    ioctl(fd, TIOCFLUSH, &out);
  }
#endif
}

