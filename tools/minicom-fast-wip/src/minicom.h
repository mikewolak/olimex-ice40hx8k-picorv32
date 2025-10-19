/*
 * minicom.h	Constants, defaults, globals etc.
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
 * fmg 1/11/94 colors
 * fmg 8/22/97 History Buffer Search support
 * jl  23.06.97 sec parameter to m_dtrtoggle
 * jl  04.09.97 conversion tables
 * jl  09.09.97 loadconv and saveconv protos
 * jl  05.10.97 changed return value of dial() to long
 * jseymour@jimsun.LinxNet.com (Jim Seymour) 03/26/98 - Added prototype
 *    for new "get_port()" function in util.c.
 */

/* First include all other application-dependent include files. */
#include "config.h"
#include "configsym.h"
#include "window.h"
#include "keyboard.h"
#include "vt100.h"
#include "libport.h"

#include <time.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>

#if HAVE_LOCKDEV
#include <ttylock.h>
#endif

#define XA_OK_EXIST	1
#define XA_OK_NOTEXIST	2

#ifndef EXTERN
# define EXTERN extern
#endif

#ifdef _UWIN2P0
EXTERN int LINES;
EXTERN int COLS;
#endif

EXTERN int dosetup;     /* In setup mode (-s) */

EXTERN char stdattr;	/* Standard attribute */

EXTERN WIN *us;		/* User screen */
EXTERN WIN *us_alternate; /* Alternate user screen */
EXTERN WIN *st;		/* Status Line */

EXTERN short terminal;	/* terminal type */
EXTERN time_t online;	/* Time online in minutes */
EXTERN long linespd;	/* Line speed */
EXTERN short portfd;	/* File descriptor of the serial port. */
EXTERN short lines;	/* Nr. of lines on the screen */
EXTERN short cols;	/* Nr. of cols of the screen */
EXTERN int keypadmode;	/* Mode of keypad */
EXTERN int cursormode;	/* Mode of cursor (arrow) keys */

EXTERN int docap;	/* Capture data to capture file */
EXTERN FILE *capfp;	/* File to capture to */
EXTERN int capbuf;	/* Buffering mode of capture file */
EXTERN int addlf;	/* Add LF after CR */
EXTERN int addcr;	/* Insert CR before LF */
EXTERN int wrapln;	/* Linewrap default */
EXTERN int display_hex; /* Display in hex */
EXTERN int tempst;	/* Status line is temporary */
EXTERN int escape;	/* Escape code. */

enum Lockfile_mode
{
  Lockfile_mode_unset   = 0,
  Lockfile_mode_uucp    = 1,
  Lockfile_mode_flock   = 2,
  Lockfile_mode_ttylock = 3,
  Lockfile_mode_max     = 4,
};
EXTERN enum Lockfile_mode lockfile_mode;
EXTERN char lockfile[270]; /* UUCP lock file of terminal */
EXTERN char homedir[256];  /* Home directory of user */
EXTERN char logfname[PARS_VAL_LEN]; /* Name of the logfile */
EXTERN char username[16];  /* Who is using minicom? */

EXTERN int bogus_dcd;	/* This indicates the dcd status if no 'real' dcd */
EXTERN int alt_override;/* -m option */

EXTERN char parfile[256]; /* Global parameter file */
EXTERN char pparfile[256]; /* Personal parameter file */

EXTERN char scr_name[256];  /* Name of last script */
EXTERN char scr_user[33];   /* Login name to use with script */
EXTERN char scr_passwd[33]; /* Password to use with script */

EXTERN char termtype[32];  /* Value of getenv("TERM"); */
EXTERN char *dial_tty;     /* tty to use. */

EXTERN char *dial_name;	    /* System we're connected to */
EXTERN char *dial_number;   /* Number we've dialed. */
EXTERN char *dial_user;     /* Our username there */
EXTERN char *dial_pass;     /* Our password */

enum Socket_type {
  Socket_type_no_socket = 0,
  Socket_type_unix = 1,
  Socket_type_tcp = 2,
};
extern enum Socket_type portfd_is_socket;	/* File descriptor is a unix socket */
extern int portfd_is_connected;	/* 1 if the socket is connected */
static inline int portfd_connected(void)
{
  return (portfd_is_socket && !portfd_is_connected) ? -1 : portfd;
}

/*
 * fmg 8/22/97
 * Search pattern can be THIS long (x characters)
 */
#define MAX_SEARCH      30

/* fmg 1/11/94 colors */

EXTERN int mfcolor;     /* Menu Foreground Color */
EXTERN int mbcolor;     /* Menu Background Color */
EXTERN int tfcolor;     /* Terminal Foreground Color */
EXTERN int tbcolor;     /* Terminal Background Color */
EXTERN int sfcolor;     /* Status Bar Foreground Color */
EXTERN int sbcolor;     /* Status Bar Background Color */
EXTERN int st_attr;	/* Status Bar attributes. */

/* jl 04.09.97 conversion tables */
extern unsigned char vt_outmap[256], vt_inmap[256];

/* MARK updated 02/17/95 - history buffer */
EXTERN int num_hist_lines;  /* History buffer size */

/* fmg 1/11/94 colors - convert color word to # */

int Jcolor(char *);

EXTERN int size_changed;     /* Window size has changed */
extern const char *Version;  /* Minicom version */

EXTERN int local_echo;      /* Local echo on/off. */

/* Forward declaration. */
struct dialent;

/* Global functions */

/* Prototypes from file: config.c */
void read_parms(void);
int  waccess(char *s);
int  config(int setup);
void get_bbp(char *ba, char *bi, char *pa, char *stopb, int curr_ok);
int update_bbp_from_char(char c, char *ba, char *bi, char *pa, char *stopb,
                         int curr_ok);
const char *yesno(int k);
int  dotermmenu(void);
int  dodflsave(void);	/* fmg - need it */
void vdodflsave(void);	/* fmg - need it */
int  domacsave(void);	/* fmg - need it */
int  loadconv(char *);	/* jl */
int  saveconv(char *);	/* jl */
int  speed_valid(unsigned int);

/* Prototypes from file: common.c */
char *pfix_home( char *s);
void do_log(const char *line, ...);
size_t one_mbtowc (wchar_t *pwc, const char *s, size_t n);
size_t one_wctomb (char *s, wchar_t wchar);
size_t mbswidth(const char *s);

/* Prototypes from file: dial.c */
void mputs(const char *s , int how);
void modeminit(void);
void modemreset(void);
void hangup(void);
void sendbreak(void);
long dial(struct dialent *d , struct dialent **d2);
int  readdialdir(void);
void dialone(char *entry);
void dialdir(void);
void free_dialents(void);

/* Prototypes from file: file.c */
char *filedir(int how_many, int downloading);
void init_dir(char dir);

/* Prototypes from file: util.c */
int fastexec(char *cmd);
int fastsystem(char *cmd, char *in, char *out, char *err);
char *get_port(char *);

/* Prototypes from file: help.c */
int help(void);

/* Prototypes from file: ipc.c */
int check_io_frontend(char *buf, int buf_size, int *bytes_red);
bool check_io_input(int timeout_ms);
int read_buf(int fd, char *buf, int bufsize);
int keyboard(int cmd, int arg);

/* Prototypes from file: keyserv.c */
void handler(int dummy);
void sendstr(char *s);

/* Prototypes from file: main.c */
extern time_t old_online;
void leave(const char *s) __attribute__((noreturn));
char *esc_key(void);
void term_socket_connect(void);
void term_socket_close(void);
int  open_term(int doinit, int show_win_on_error, int no_msgs);
void init_emul(int type, int do_init);
void timer_update(void);
void show_status(void);
void set_status_line_format(const char *s);
void scriptname(const char *s);
int  do_terminal(void);
void status_set_display(const char *text, int duration_s);

/* Prototypes from file: minicom.c */
void port_init(void);
void toggle_addlf(void);
void toggle_local_echo(void);
void set_local_echo(int val);
void set_addlf(int val);
void set_addcr(int val);

void drawhist_look(WIN *w, int y, int r, wchar_t *look, int case_matters);
void searchhist(WIN *w_hist, wchar_t *str);
int  find_next(WIN *w, WIN *w_hist, int hit_line, wchar_t *look,
               int case_matters);
const wchar_t *upcase(wchar_t *dest, wchar_t *src);
wchar_t *StrStr(wchar_t *str1, wchar_t *str2, int case_matters);

void do_iconv(char **inbuf, size_t *inbytesleft,
              char **outbuf, size_t *outbytesleft);
int  using_iconv(void);

/* Prototypes from file: rwconf.c */
int writepars(FILE *fp, int all);
int writemacs(FILE *fp);
int readpars(FILE *fp, enum config_type conftype);
int readmacs(FILE *fp, int init); /* fmg */

/* Prototypes from file: sysdep1.c */
void m_sethwf(int fd, int on);
void m_dtrtoggle(int fd, int sec);
void m_break(int fd);
int  m_getdcd(int fd);
void m_setdcd(int fd, int what);
void m_savestate(int fd);
void m_restorestate(int fd);
void m_nohang(int fd);
void m_hupcl(int fd, int on);
void m_flush(int fd);
void m_flush_script( int fd);
unsigned m_getmaxspd(void);
void m_setparms(int fd, char *baudr, char *par, char *bits, char *stopb,
                int hwf, int swf, int rs485en);
void m_set485parms(int fd, int en, int rts_on_snd, int rts_aft_snd,
                   int rx_dur_tx, int term_bus, char *del_rts_bef_snd,
                   char *del_rts_aft_snd);
int  m_wait(int *st);

/* Prototypes from file: sysdep2.c */
void getrowcols(int *rows, int *cols);
int  setcbreak(int mode);
void enab_sig(int onoff, int intrchar);

/* Prototypes from file: updown.c */
void updown(int what, int nr );
int  mc_setenv(const char *, const char *);
void kermit(void);
void runscript(int ask, const char *s, const char *l, const char *p);
int  paste_file(void);

/* Prototypes from file: fast-xfr.c */
int fast_upload(int fd, const char *filename);
int fast_download(int fd, const char *filename);

/* Prototypes from file: windiv.c */
WIN *mc_tell(const char *, ...);
void werror(const char *, ...);
int ask(const char *what, const char *s[]);
char *input(char *s, char *buf, size_t bufsize);

/* Prototypes from file: wildmat.c */
int wildmat(const char *, const char *);

/* Prototypes from file: wkeys.c */
extern int io_pending, pendingkeys;

/* Prototypes from file: config.c */
void domacros(void);

void device_close(void);

int lockfile_create(void);
void lockfile_remove(void);

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))


/* We want the ANSI offsetof macro to do some dirty stuff. */
#ifndef offsetof
#  define offsetof(type, member) ((int) &((type *)0)->member)
#endif

/* Values for the "flags". */
#define FL_ECHO		0x01	/* Local echo on/off. */
#define FL_DEL		0x02	/* Backspace or DEL */
#define FL_WRAP		0x04	/* Use autowrap. */
#define FL_ANSI		0x08	/* Type of term emulation */
#define FL_TAG		0x80	/* This entry is tagged. */
#define FL_SAVE		0x0f	/* Which portions of flags to save. */

enum {
  TIMESTAMP_LINE_OFF,
  TIMESTAMP_LINE_SIMPLE,
  TIMESTAMP_LINE_EXTENDED,
  TIMESTAMP_LINE_PER_SECOND,
  TIMESTAMP_LINE_DELTA,
  TIMESTAMP_LINE_NR_OF_OPTIONS, // must be last
};

int toggle_line_timestamp(void);
const char *line_timestamp_description(void);
const char *timestamp_option_idstring(const int o);


/* Enhanced snprintf function -- as from Linux */
int vscnprintf(char *buf, size_t size, const char *fmt, va_list args);
int scnprintf(char *buf, size_t size, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));
