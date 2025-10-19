#ifndef H_LIBPORT
#define H_LIBPORT

#include <stdarg.h>
#include <config.h>

#ifndef HAVE_SNPRINTF
extern int snprintf (char *str, size_t count, const char *fmt, ...);
#endif

#ifndef HAVE_VSNPRINTF
extern int vsnprintf (char *str, size_t count, const char *fmt, va_list arg);
#endif

#ifndef HAVE_USLEEP
int usleep(unsigned usecs);
#endif

#endif /* !HLIB_PORT */
