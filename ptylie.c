#define _XOPEN_SOURCE 600
#include "config.h"
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
/* For FIONREAD */
#if defined(__sun)
#include <sys/filio.h>
#endif
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <termios.h>
#include <term.h>
#include <unistd.h>

#include "tree.h"

#define MAX_ETIME 86400

typedef struct stk_s stk_t;

typedef struct map_elem_s map_elem_t;

/* ---------- */
/* Prototypes */
/* ---------- */

void
stk_init(stk_t * stack);

int
stk_empty(stk_t * stack);

int
stk_push(stk_t * stack, int fd);

int
stk_pop(stk_t * stack);

static void
tv_norm(struct timeval * tv);

static void
tv_add(struct timeval * tv1, struct timeval * tv2);

static void
tv_sub(struct timeval * tv1, struct timeval * tv2);

void
usage(char * prog);

void
cleanup(void);

void
handler(int sig);

void
msg(int type, const char * message, ...);

int
tty_raw(struct termios * attr, int fd);

int
open_master(void);

void
read_write(const char * name, int in, int out, int log);

void
set_terminal_size(int fd, unsigned width, unsigned height);

void
set_terminal(int fd, struct termios * old_termios);

void *
manage_io(void * args);

void
get_arg(int fd, unsigned char * buf, int * len);

void *
inject_keys(void * args);

void
master(int fd_master, int fd_slave, int fdl, int fdc);

void
slave(int fd_slave, char ** argv);

int
badopt(const char * mess, int ch);

int
my_getopt(int argc, char * argv[], const char * optstring);

void
init_etime(void);

void
add_srt_entry(char * buf);

int
main(int argc, char * argv[]);

/* ----------- */
/* Definitions */
/* ----------- */

struct timeval first; /* real time */

/* Terminal settings backups */
/* """"""""""""""""""""""""" */
struct termios old_termios_master;
struct termios old_termios_slave;

int fd_termios;

char * my_optarg;     /* global argument pointer. */
int    my_optind = 0; /* global argv index. */
int    my_opterr = 1; /* for compatibility, should error be printed? */
int    my_optopt;     /* for compatibility, option character checked */

struct timeval srt_offset  = { 0 };
long           offset      = 0;
int            offset_sign = 1;

enum
{
  WARN,
  FATAL
};

/* Structure to pass to thread functions */
/* """"""""""""""""""""""""""""""""""""" */
struct args_s
{
  int fd1;
  int fd2;
};

/* 255 int positions stack */
/* """"""""""""""""""""""" */
struct stk_s
{
  int nb;
  int stack[255];
};

struct map_elem_s
{
  char * key;
  char * repl;
};

rb_tree * map_tree;

const char * prog = "ptylie";
char *       scan = NULL; /* Private scan pointer. */

FILE * srt = NULL;
FILE * map = NULL;

int srt_on           = 0;
int default_duration = 300; /* ms */
int duration;

char * log_file = NULL;
char * srt_file = NULL;

/* ------------------------------ */
/* int stack management functions */
/* ------------------------------ */

void
stk_init(stk_t * stack)
{
  stack->nb = 0;
}

int
stk_empty(stk_t * stack)
{
  return (stack->nb == 0);
}

int
stk_push(stk_t * stack, int fd)
{
  if (stack->nb == 255)
    return -1;

  stack->stack[stack->nb] = fd;
  stack->nb++;

  return fd;
}

int
stk_pop(stk_t * stack)
{
  if (stack->nb == 0)
    return -1;

  stack->nb--;

  return stack->stack[stack->nb];
}

/* ----------------------------- */
/* Timeval management functions. */
/* ----------------------------- */

/* ==================================== */
/* Ensures that 0 <= tv_usec < 1000000. */
/* ==================================== */
static void
tv_norm(struct timeval * tv)
{
  if (tv->tv_usec >= 1000000)
  {
    tv->tv_sec += tv->tv_usec / 1000000;
    tv->tv_usec %= 1000000;
  }
  else if (tv->tv_usec < 0)
  {
    tv->tv_usec = 0;
  }
}

/* ======================= */
/* timeval add tv2 to tv1. */
/* ======================= */
static void
tv_add(struct timeval * tv1, struct timeval * tv2)
{
  /* Ensures that 0 <= usec < 1000000. */
  /* """"""""""""""""""""""""""""""""" */
  tv_norm(tv1);
  tv_norm(tv2);

  tv1->tv_sec += tv2->tv_sec;
  tv1->tv_usec += tv2->tv_usec;

  if (tv1->tv_usec >= 1000000)
  {
    tv1->tv_sec++;
    tv1->tv_usec -= 1000000;
  }
}

/* ========================= */
/* timeval sub tv2 from tv1. */
/* ========================= */
static void
tv_sub(struct timeval * tv1, struct timeval * tv2)
{
  /* Ensures that 0 <= usec < 1000000. */
  /* """"""""""""""""""""""""""""""""" */
  tv_norm(tv1);
  tv_norm(tv2);

  tv1->tv_sec -= tv2->tv_sec;
  tv1->tv_usec -= tv2->tv_usec;

  if (tv1->tv_usec < 0)
  {
    tv1->tv_sec--;
    tv1->tv_usec += 1000000;
  }
}

/* =============== */
/* init the timer. */
/* =============== */
void
init_etime(void)
{
  gettimeofday(&first, NULL);
  if (offset_sign > 0)
    tv_sub(&first, &srt_offset);
  else
    tv_add(&first, &srt_offset);
}

/* ================================================ */
/* return elapsed seconds since call to init_etime. */
/* ================================================ */
void
add_srt_entry(char * buf)
{
  static unsigned c = 1;
  int             h1, m1;
  int             h2, m2;
  long            s1, ms1;
  long            s2, ms2;

  long etime;

  struct timeval curr;

  gettimeofday(&curr, NULL);
  etime =
    (curr.tv_sec - first.tv_sec) * 1000000L + (curr.tv_usec - first.tv_usec);

  etime /= 1000;

  h1  = (int)(etime / 3600000);
  m1  = (int)((etime - h1 * 3600000) / 60000);
  s1  = (etime - h1 * 3600000 - m1 * 60000) / 1000;
  ms1 = etime - h1 * 3600000 - m1 * 60000 - s1 * 1000;

  etime += duration;

  h2  = (int)(etime / 3600000);
  m2  = (int)((etime - h2 * 3600000) / 60000);
  s2  = (etime - h2 * 3600000 - m2 * 60000) / 1000;
  ms2 = etime - h2 * 3600000 - m2 * 60000 - s2 * 1000;

  fprintf(srt,
          "%u\n"
          "%02d:%02d:%02ld,%03ld --> "
          "%02d:%02d:%02ld,%03ld\n"
          "%s\n\n",
          c++, h1, m1, s1, ms1, h2, m2, s2, ms2, buf);
}

/* ----------------- */
/* Utility functions */
/* ----------------- */

/* ========================================================= */
/* Decode the number of bytes taken by a character (UTF-8)   */
/* It is the length of the leading sequence of bits set to 1 */
/* (Count Leading Ones)                                      */
/* ========================================================= */
static int
mb_get_length(unsigned char c)
{
  if (c >= 0xf0)
    return 4;
  else if (c >= 0xe0)
    return 3;
  else if (c >= 0xc2)
    return 2;
  else
    return 1;
}

static const char trailing_bytes_for_utf8[256] = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
  2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5
};

/* ================================================================= */
/* UTF8 validation routine inspired by Jeff Bezanson                 */
/*   placed in the public domain Fall 2005                           */
/*   (https://github.com/JeffBezanson/cutef8)                        */
/*                                                                   */
/* Returns 1 if str contains a valid UTF8 byte sequence, 0 otherwise */
/* ================================================================= */
static int
mb_validate(const char * str, int length)
{
  const unsigned char *p, *pend = (unsigned char *)str + length;
  unsigned char        c;
  int                  ab;

  for (p = (unsigned char *)str; p < pend; p++)
  {
    c = *p;
    if (c < 128)
      continue;
    if ((c & 0xc0) != 0xc0)
      return 0;
    ab = trailing_bytes_for_utf8[c];
    if (length < ab)
      return 0;
    length -= ab;

    p++;
    /* Check top bits in the second byte */
    /* """"""""""""""""""""""""""""""""" */
    if ((*p & 0xc0) != 0x80)
      return 0;

    /* Check for overlong sequences for each different length */
    /* """""""""""""""""""""""""""""""""""""""""""""""""""""" */
    switch (ab)
    {
      /* Check for xx00 000x */
      /* """"""""""""""""""" */
      case 1:
        if ((c & 0x3e) == 0)
          return 0;
        continue; /* We know there aren't any more bytes to check */

      /* Check for 1110 0000, xx0x xxxx */
      /* """""""""""""""""""""""""""""" */
      case 2:
        if (c == 0xe0 && (*p & 0x20) == 0)
          return 0;
        break;

      /* Check for 1111 0000, xx00 xxxx */
      /* """""""""""""""""""""""""""""" */
      case 3:
        if (c == 0xf0 && (*p & 0x30) == 0)
          return 0;
        break;

      /* Check for 1111 1000, xx00 0xxx */
      /* """""""""""""""""""""""""""""" */
      case 4:
        if (c == 0xf8 && (*p & 0x38) == 0)
          return 0;
        break;

      /* Check for leading 0xfe or 0xff,   */
      /* and then for 1111 1100, xx00 00xx */
      /* """"""""""""""""""""""""""""""""" */
      case 5:
        if (c == 0xfe || c == 0xff || (c == 0xfc && (*p & 0x3c) == 0))
          return 0;
        break;
    }

    /* Check for valid bytes after the 2nd, if any; all must start 10 */
    /* """""""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
    while (--ab > 0)
    {
      if ((*(++p) & 0xc0) != 0x80)
        return 0;
    }
  }

  return 1;
}

/* ======================================================================== */
/* unicode (UTF-8) ascii representation interprer.                          */
/* The string passed will be altered but will not move in memory            */
/* All sequence of \uxx, \uxxxx, \uxxxxxx and \uxxxxxxxx will be replace by */
/* the corresponding UTF-8 character.                                       */
/* ======================================================================== */
void
mb_interpret(char * s)
{
  char * utf8_str;          /* \uxx...                                        */
  size_t utf8_to_eos_len;   /* bytes in s starting from the first             *
                             * occurrence of \u                               */
  size_t init_len;          /* initial lengths of the string to interpret     */
  size_t utf8_ascii_len;    /* 2,4,6 or 8 bytes                               */
  size_t len_to_remove = 0; /* number of bytes to remove after the conversion */
  char   tmp[9];            /* temporary string                               */

  /* Guard against the case where s is NULL */
  /* """""""""""""""""""""""""""""""""""""" */
  if (s == NULL)
    return;

  init_len = strlen(s);

  while ((utf8_str = strstr(s, "\\u")) != NULL)
  {
    utf8_to_eos_len = strlen(utf8_str);
    if (utf8_to_eos_len
        < 4) /* string too short to contain a valid UTF-8 char */
    {
      *utf8_str       = '.';
      *(utf8_str + 1) = '\0';
    }
    else /* s is long enough */
    {
      unsigned byte;
      char *   utf8_seq_offset = utf8_str + 2;

      /* Get the first 2 utf8 bytes */
      *tmp       = *utf8_seq_offset;
      *(tmp + 1) = *(utf8_seq_offset + 1);
      *(tmp + 2) = '\0';

      /* If they are invalid, replace the \u sequence by a dot */
      /* """"""""""""""""""""""""""""""""""""""""""""""""""""" */
      if (!isxdigit(tmp[0]) || !isxdigit(tmp[1]))
      {
        *utf8_str = '.';
        if (4 >= utf8_to_eos_len)
          *(utf8_str + 1) = '\0';
        else
          memmove(utf8_str, utf8_str + 4, utf8_to_eos_len - 4);
        return;
      }
      else
      {
        /* They are valid, deduce from them the length of the sequence */
        /* """"""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
        sscanf(tmp, "%2x", &byte);
        utf8_ascii_len = mb_get_length(byte) * 2;

        /* Check again if the inputs string is long enough */
        /* """"""""""""""""""""""""""""""""""""""""""""""" */
        if (utf8_to_eos_len - 2 < utf8_ascii_len)
        {
          *utf8_str       = '.';
          *(utf8_str + 1) = '\0';
        }
        else
        {
          /* replace the \u sequence by the bytes forming the UTF-8 char */
          /* """"""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
          size_t i;
          *tmp = byte;

          /* Put the bytes in the tmp string */
          /* ''''''''''''''''''''''''''''''' */
          for (i = 1; i < utf8_ascii_len / 2; i++)
          {
            sscanf(utf8_seq_offset + 2 * i, "%2x", &byte);
            *(tmp + i) = byte;
          }
          tmp[utf8_ascii_len / 2] = '\0';

          /* Does they form a valid UTF-8 char? */
          /* '''''''''''''''''''''''''''''''''' */
          if (mb_validate(tmp, utf8_ascii_len / 2))
          {
            /* Put them back in the original string and move */
            /* the remaining bytes after them                */
            /* ''''''''''''''''''''''''''''''''''''''''''''' */
            memmove(utf8_str, tmp, utf8_ascii_len / 2);

            if (utf8_to_eos_len < utf8_ascii_len)
              *(utf8_str + utf8_ascii_len / 2 + 1) = '\0';
            else
              memmove(utf8_str + utf8_ascii_len / 2,
                      utf8_seq_offset + utf8_ascii_len,
                      utf8_to_eos_len - utf8_ascii_len - 2 + 1);
          }
          else
          {
            /* The invalid sequence is replaced by a dot */
            /* ''''''''''''''''''''''''''''''''''''''''' */
            *utf8_str = '.';
            if (utf8_to_eos_len < utf8_ascii_len)
              *(utf8_str + 1) = '\0';
            else
              memmove(utf8_str + 1, utf8_seq_offset + utf8_ascii_len,
                      utf8_to_eos_len - utf8_ascii_len - 2 + 1);
            utf8_ascii_len = 2;
          }
        }

        /* Update the number of bytes to remove at the end */
        /* of the initial string                           */
        /* """"""""""""""""""""""""""""""""""""""""""""""" */
        len_to_remove += 2 + utf8_ascii_len / 2;
      }
    }
  }

  /* Make sure that the string is well terminated */
  /* """""""""""""""""""""""""""""""""""""""""""" */
  *(s + init_len - len_to_remove) = '\0';

  return;
}
/* =============================================== */
/* Displays a small help and terminate the program */
/* =============================================== */
void
usage(char * prog)
{
  fprintf(stderr,
          "Usage: %s [-l log_file] "
          "[-w terminal_width] "
          "[-h terminal_height] \\\n"
          "         -i command_file program_to_launch "
          "program_arguments\n",
          prog);
  exit(EXIT_FAILURE);
}

/* =========================================================== */
/* Restores the terminal as it was before running the program. */
/* =========================================================== */
void
cleanup(void)
{
  tcsetattr(fd_termios, TCSANOW, &old_termios_master);
  tcsetattr(fd_termios, TCSANOW, &old_termios_slave);
}

/* ===================== */
/* SIGINT signal handler */
/* ===================== */
void
handler(int sig)
{
  cleanup();
  exit(EXIT_FAILURE);
}

/* ================================================================ */
/* printf like function to display fatal messages and terminate the */
/* program.                                                         */
/* ================================================================ */
void
msg(int type, const char * message, ...)
{
  va_list args;

  va_start(args, message);
  vfprintf(stderr, message, args);
  fputc('\n', stderr);
  va_end(args);

  if (type == FATAL)
    exit(EXIT_FAILURE);
}

/* ============================================================ */
/* Sets the terminal fd in raw mode, returns -1 on failure. */
/* ============================================================ */
int
tty_raw(struct termios * attr, int fd)
{
  attr->c_lflag &= ~(ECHO | ICANON | IEXTEN);
  attr->c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  attr->c_cflag &= ~(CSIZE | PARENB);
  attr->c_cflag |= (CS8);
  attr->c_oflag &= ~(OPOST);
  attr->c_cc[VMIN]  = 1;
  attr->c_cc[VTIME] = 0;

  return tcsetattr(fd, TCSANOW, attr);
}

/* ================================= */
/* Obtain the master file descriptor */
/* ================================= */
int
open_master(void)
{
  int rc, fd_master;

  fd_master = posix_openpt(O_RDWR);
  if (fd_master < 0)
    msg(FATAL, "Error %d on posix_openpt()", errno);

  rc = grantpt(fd_master);
  if (rc != 0)
    msg(FATAL, "Error %d on grantpt()", errno);

  rc = unlockpt(fd_master);
  if (rc != 0)
    msg(FATAL, "Error %d on unlockpt()", errno);

  return fd_master;
}

/* ======================================================= */
/* Utility function that Reads bytes from in an write them */
/* to out and log                                          */
/* ======================================================= */
void
read_write(const char * name, int in, int out, int log)
{
  char input[128];
  int  rc;

  rc = read(in, input, sizeof input);
  if (rc < 0)
  {
    if (errno == EIO)
      exit(0);

    msg(FATAL, "Error %d on read %s", errno, name);
  }

  write(out, input, rc);
  write(log, input, rc);
}

void
set_terminal_size(int fd, unsigned width, unsigned height)
{
  struct winsize ws = { 24, 80, 0, 0 };

  /* Set the default terminal geometry */
  /* """"""""""""""""""""""""""""""""" */
  if (height == 0 && width == 0)
  {
    height = 24;
    width  = 80;
  }

  /* Set terminal size to the pty. */
  /* """"""""""""""""""""""""""""" */
  ws.ws_row = height;
  ws.ws_col = width;
  ioctl(fd, TIOCSWINSZ, &ws);
}

/* ======================= */
/* Terminal initialization */
/* ======================= */
void
set_terminal(int fd, struct termios * old_termios)
{
  struct termios   new_termios;
  int              rc;
  struct sigaction sa;

  if (isatty(0))
    fd_termios = 0;
  else if (isatty(1))
    fd_termios = 1;
  else
    return;

  /* Save the defaults parameters to be able to restore then on exit */
  /* and in case of reception of an INT signal.                      */
  rc = tcgetattr(fd, old_termios);
  if (rc == -1)
    msg(FATAL, "Error %d on tcgetattr()", errno);

  /* Setup the signal handler */
  /* """""""""""""""""""""""" */
  sa.sa_handler = handler;
  sa.sa_flags   = 0;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGINT, &sa, NULL);

  /* Set RAW mode */
  /* """""""""""" */
  new_termios = *old_termios;
  if (tty_raw(&new_termios, 0) == -1)
    msg(FATAL, "Cannot set %s in raw mode", fd);
}

/* ================================================================= */
/* This function is responsible to send and receive io in the master */
/* part. The hard work is done by the read_write function.           */
/* ================================================================= */
void *
manage_io(void * args)
{

  fd_set fd_in;
  int    fd_master = ((struct args_s *)args)->fd1;
  int    fdl       = ((struct args_s *)args)->fd2;

  for (;;)
  {
    /* Wait for data from standard input and master side of PTY */
    /* """""""""""""""""""""""""""""""""""""""""""""""""""""""" */
    FD_ZERO(&fd_in);
    FD_SET(0, &fd_in);
    FD_SET(fd_master, &fd_in);

    if (select(fd_master + 1, &fd_in, NULL, NULL, NULL) == -1)
      msg(FATAL, "Error %d on select()", errno);

    /* If data on standard input */
    /* """"""""""""""""""""""""" */
    if (FD_ISSET(0, &fd_in))
      read_write("standard input", 0, fd_master, fdl);

    /* If data on master side of PTY */
    /* """"""""""""""""""""""""""""" */
    if (FD_ISSET(fd_master, &fd_in))
      read_write("master pty", fd_master, 1, fdl);
  }

  return NULL;
}

/* ===================================================================== */
/* get the directive argument that must be found starting at the current */
/* fd position and ending before the next ']'.                           */
/*                                                                       */
/* fd  (IN)  file desctiptor to work on.                                 */
/* buf (OUT) output null terminated buffer.                              */
/* len (OUT) size in byte of the argument found.                         */
/*                                                                       */
/* NOTE: a missing trailing ']' is silently ignored and at most 4096     */
/*       byte will be read.                                              */
/*                                                                       */
/* TODO: Make this more robust and generic                               */
/* ===================================================================== */
void
get_arg(int fd, unsigned char * buf, int * len)
{
  int data = '\0';
  int rc   = 0;

  *len = 0;

  while (rc != -1 && data != ']' && *len < 4096)
  {
    rc = read(fd, &data, 1);
    if (rc == -1 || rc == 0)
      break;
    buf[(*len)++] = (unsigned char)data;
  }
  buf[*len] = '\0';
}

int
map_elem_comp(const void * ptr1, const void * ptr2)
{
  map_elem_t * d1 = (map_elem_t *)ptr1;
  map_elem_t * d2 = (map_elem_t *)ptr2;
  return strcmp(d1->key, d2->key);
}

void
map_elem_free(void * ptr)
{
  map_elem_t * d = (map_elem_t *)ptr;
  free(d->key);
  free(d->repl);
}

/* ================================================================= */
/* Injects keys in the slave's keyboard buffer, we need to have root */
/* privileges to do that.                                            */
/* Manages also some special additional directives (\s, \S, ...)     */
/* ================================================================= */
void *
inject_keys(void * args)
{
  int           data;
  int           rc;
  int           i;
  int           l, len;
  int           n;
  unsigned char c;
  unsigned char buf[4096];
  unsigned char vbuf[4096];
  unsigned char scanf_buf[4096];
  char          tmp[256];
  char          rows[4], cols[4];
  int           special      = 0;
  int           meta         = 0;
  int           control      = 0;
  long          sleep_time   = 0;
  time_t        sleep_time_s = 0;
  long          sleep_time_n = 0;

  char * v_empty = "";
  char * v_space = "\xe2\x90\xa3";
  char * v_ht    = "\xe2\x87\xa5";
  char * v_lf    = "\xe2\x8f\x8e";
  char * v_cr    = "\xe2\x90\x8d";
  char * v_bs    = "\xe2\x8c\xab";
  char * v_esc   = "ESC";

  char * format;

  char * srt_buf_prt = v_empty;

  stk_t fd_stack; /* int stack to remember nested file descriptors *
                   * used by the 'R' command                       */

  int fd  = ((struct args_s *)args)->fd1;
  int fdc = ((struct args_s *)args)->fd2;

  struct winsize ws;

  rb_tree *  map_tree = new_rb_tree(map_elem_comp);
  map_elem_t elem;

  stk_init(&fd_stack);

  /* Sleep for 1/10 s to let a chance to the child program to start.  */
  /* If it is not enough you can always begin the command file with a */
  /* appropriate sleep directive (\S[...].                            */
  /* """""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
  nanosleep((const struct timespec[]){ { 0, 100000000L } }, NULL);

  /* parse the command file and send keystokes to the child or sleep */
  /* for an amount of time.                                          */
  /* """"""""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
  for (;;)
  {
    rc = read(fdc, &data, 1);
    if (rc == -1)
      break;
    if (rc == 0)
    {
      close(fdc);
      fdc = stk_pop(&fd_stack);
      if (fdc == -1)
        break;
      else
        continue;
    }

    c = (unsigned char)data;

    if (srt_on)
      vbuf[0] = '\0';

    if (!special)
    {
      if (c == '\\')
      {
        special = 1;
        continue;
      }

      if (meta)
      {
        meta   = 0;
        buf[0] = 0x1b;
        buf[1] = c;

        if (srt_on)
        {
          strcpy((char *)vbuf, "ALT-");
          vbuf[4] = toupper(c);
          vbuf[5] = '\0';
        }

        l = 2;
      }
      else if (control)
      {
        control = 0;
        buf[0]  = toupper(c) - '@';

        if (srt_on)
        {
          vbuf[0] = '^';
          vbuf[1] = toupper(c);
          vbuf[2] = '\0';
        }

        l = 1;
      }
      else
      {
        int i;

        buf[0] = c;
        l      = mb_get_length(c);

        if (l > 1)
        {
          int bread;
          int bsofar = 0;
          do
          {
            bread = read(fdc, &buf[bsofar + 1], l - 1 - bsofar);
            bsofar += bread;
          } while (bsofar < l - 1 && bread > 0);
        }
      }
    }
    else
    {
      l       = 1;
      special = 0;
      char * v[10 + 1];
      long   q[9];

      switch (c)
      {
        case '\n':
          goto loop;

        case '\\':
          buf[0] = '\\';
          break;

        case 's': /* set new seep time between keytrokes        */
        case 'S': /* sleep for the given amount of milliseconds */
          get_arg(fdc, scanf_buf, &len);
          n = sscanf((char *)scanf_buf, "[%5[0-9]]%n", tmp, &l);
          if (n != 1)
            exit(EXIT_FAILURE);

          if (c == 'S')
          {
            long   delay_ms = atol(tmp);       /* milliseconds */
            time_t s        = delay_ms / 1000; /* seconds */

            nanosleep((const struct timespec[]){ { s, delay_ms * 1000000L
                                                        - s * 1000000000L } },
                      NULL);
            continue;
          }
          else
          {
            sleep_time   = atol(tmp);          /* milliseconds */
            sleep_time_s = sleep_time / 1000L; /* seconds */
            sleep_time_n = sleep_time * 1000000L
                           - sleep_time_s * 1000000000L; /* remaining ns */
            goto loop;
          }

        case 'W': /* for terminal resizing (ex: [80x24] */
          get_arg(fdc, scanf_buf, &len);
          n = sscanf((char *)scanf_buf, "[%3[0-9]x%3[0-9]]", cols, rows);
          if (n != 2)
            exit(EXIT_FAILURE);
          memset(&ws, 0, sizeof ws);
          ws.ws_row = atoi(rows);
          ws.ws_col = atoi(cols);
          ioctl(fd, TIOCSWINSZ, &ws);
          goto loop;

        case 'R': /* include a bytes sequence form a given file, beware *
                   * to trailing newlines.                              */
        {
          int fd_include;
          get_arg(fdc, scanf_buf, &len);
          if (scanf_buf[0] != '\0')
          {
            if (scanf_buf[strlen((char *)scanf_buf + 1)] == ']')
              scanf_buf[strlen((char *)scanf_buf + 1)] = '\0';

            if ((fd_include = open((char *)scanf_buf + 1, O_RDONLY)) == -1)
            {
              msg(FATAL, "\r\nCannot open include file %s\r\n", scanf_buf + 1);
            }
            else
            {
              stk_push(&fd_stack, fdc);
              fdc = fd_include;
              continue;
            }
          }
        }
        break;

        case 'm': /* consider a new map file */
        {
          get_arg(fdc, scanf_buf, &len);
          if (scanf_buf[0] != '\0')
          {
            if (scanf_buf[strlen((char *)scanf_buf + 1)] == ']')
              scanf_buf[strlen((char *)scanf_buf + 1)] = '\0';

            if ((map = fopen((char *)scanf_buf + 1, "r")) == NULL)
            {
              msg(FATAL, "\r\nCannot open map file %s\r\n", scanf_buf + 1);
            }
            else
            {
              char line[256];
              char key[256], repl[256];

              rb_tree_remove_all(map_tree, map_elem_free);

              while (!feof(map))
              {
                fscanf(map, "%255[^\n]\n", line);
                line[255] = '\0';

                if (sscanf(line, "%255[^ ] %255[^\n]\n", key, repl) != 2)
                  continue;
                else
                {
                  key[255] = repl[255] = '\0';

                  map_elem_t * elem = malloc(sizeof(map_elem_t));
                  elem->key         = strdup(key);
                  mb_interpret(elem->key);
                  elem->repl = strdup(repl);
                  mb_interpret(elem->repl);
                  rb_tree_insert(map_tree, elem);
                }
              }
              continue;
            }
          }
        }

        break;

        case 'x': /* Arbitrary hexadecimal sequence (max 256) */
        case 'u': /* for raw hexadecimal UTF-8 injection \u[xx[yy[zz[tt]]]]*/
          if (c == 'x')
            format = "[%256[0-9a-fA-F]]%n";
          else
            format = "[%8[0-9a-fA-F]]%n";

          get_arg(fdc, scanf_buf, &len);
          n = sscanf((char *)scanf_buf, format, tmp, &l);

          if (n != 1)
            msg(FATAL, "\r\nInvalid sequence.\r\n", scanf_buf + 1);

          if (l < 2 || l % 2 == 1)
            msg(FATAL, "\r\nInvalid hexadecimal sequence.\r\n", scanf_buf + 1);

          for (i = 0; i < (l - 2) / 2; i++)
          {
            unsigned char charhex[3] = { tmp[i * 2], tmp[i * 2 + 1], 0 };
            tmp[i] = (unsigned char)strtol((char *)charhex, NULL, 16);
          }

          l      = (l - 2) / 2;
          tmp[l] = '\0';
          memcpy(buf, tmp, l);

          break;

        case 'T':
        {
          char * p;
          int    i = 0, j;

          get_arg(fdc, scanf_buf, &len);
          n = sscanf((char *)scanf_buf, "[%256[^]]]", tmp);
          for (p = tmp; i < 10;)
          {
            while (isspace(*(unsigned char *)p))
              p++;
            if (*p == '\0')
              break;
            v[i++] = p;
            while (!isspace(*(unsigned char *)p) && *p != '\0')
              ++p;
            if (*p == '\0')
              break;
            *p++ = '\0';
          }
          v[i] = NULL;

          if ((p = (char *)tigetstr(v[0])) != (char *)-1)
          {
            char * end;

            if (p == NULL)
            {
              buf[0] = '\0';
              l      = 0;
              break;
            }
            else
            {
              q[0] = q[1] = q[2] = q[3] = q[4] = q[5] = q[6] = q[7] = q[8] = 0L;

              for (j = 1; j < i - 2; j++)
              {
                if (v[j] != NULL)
                {
                  q[j - 1] = strtol(v[j], &end, 0);
                  if (*end != '\0')
                    q[j - 1] = (long)v[j];
                }
              }
              strcpy(buf, tparm(p, q[0], q[1], q[2], q[3], q[4], q[5], q[6],
                                q[7], q[8]));
              l = strlen(buf);
            }
          }
          else
          {
            buf[0] = '\0';
            l      = 0;
          }
        }
        break;

        case 'c': /* colour setting \c[x;y;z] */
          buf[0] = 0x1b;
          buf[1] = '[';
          l      = 2;
          get_arg(fdc, scanf_buf, &len);
          n = sscanf((char *)scanf_buf, "[%8[0-9;]]", tmp);
          if (n != 1)
            exit(EXIT_FAILURE);

          while (l < len)
          {
            buf[l] = tmp[l - 2];
            l++;
          }
          buf[l] = 'm';
          l++;
          break;

        case 'k': /* keys as subtitle on/off */
          if (srt == NULL)
            srt = fopen(srt_file, "w");

          srt_on = !srt_on;
          continue;

        case 'M':
          meta = 1;
          continue;

        case 'C':
          control = 1;
          continue;

        case '%':
          break;

        case 'r':
          buf[0] = '\r';
          break;

        case '\"':
          buf[0] = '"';
          if (srt_on)
            break;

        case '\'':
          buf[0] = '\'';
          if (srt_on)
            break;

        case 'a': /* ignored */
          continue;

        case 't':
          buf[0] = '\t';
          break;

        case 'n':
          buf[0] = '\n';
          break;

        case 'e':
          buf[0] = 0x1b;
          break;

        case 'b':
        case 'h':
          buf[0] = '\b';
          break;
      }
    }

    /* Close the buffer and inject it */
    /* """""""""""""""""""""""""""""" */
    buf[l] = '\0';

    if (srt_on)
    {
      if (map != NULL)
      {
        map_elem_t * pelem;
        elem.key = (char *)buf;
        mb_interpret(elem.key);
        if ((pelem = rb_tree_search(map_tree, &elem)) != NULL)
          strcpy((char *)vbuf, pelem->repl);
      }
    }

    /* Wait for an empty input queue to continue */
    /* ''''''''''''''''''''''''''''''''''''''''' */
    {
      int chars;

      ioctl(fd, FIONREAD, &chars);
      while (chars > 1)
      {
        nanosleep((const struct timespec[]){ { 0, 50000L } }, NULL);
        ioctl(fd, FIONREAD, &chars);
      }
    }

    if (l > 1)
    {
      unsigned char * p;
      for (p = buf; *p; p++)
        if (ioctl(fd, TIOCSTI, p) < 0)
          exit(EXIT_FAILURE);

      if (srt_on)
      {
        if (*vbuf != '\0')
          add_srt_entry((char *)vbuf);
        else if (mb_validate((char *)buf, l))
          add_srt_entry((char *)buf);
      }
    }
    else
    {
      if (srt_on)
      {
        if (*vbuf != '\0')
          add_srt_entry((char *)vbuf);
        else if (isgraph(buf[0]))
          add_srt_entry((char *)buf);
        else
        {
          if (map == NULL)
          {
            switch (buf[0])
            {
              case ' ':
                srt_buf_prt = v_space;
                break;

              case 0x09:
                srt_buf_prt = v_ht;
                break;

              case 0x0d:
                srt_buf_prt = v_cr;
                break;

              case 0x0a:
                srt_buf_prt = v_lf;
                break;

              case 0x1b:
                srt_buf_prt = v_esc;
                break;

              case '\b':
              case 0x7f:
                srt_buf_prt = v_bs;
                break;

              default:
                sprintf((char *)vbuf, "<0x%2x>", c);
                srt_buf_prt = (char *)vbuf;
                break;
            }
            add_srt_entry(srt_buf_prt);
          }
        }
      }

      if (ioctl(fd, TIOCSTI, buf) < 0)
        exit(EXIT_FAILURE);
    }

    /* inter injection loop 1/20 s min to leave the application */
    /* the time to read the keyboard.                           */
    /* """""""""""""""""""""""""""""""""""""""""""""""""""""""" */

  loop:

    /* default to 1/20 s when sleep_time is set to 0 */
    /* ''''''''''''''''''''''''''''''''''''''''''''''' */
    if (sleep_time < 20)
    {
      sleep_time   = 20;
      sleep_time_s = 0;
      sleep_time_n = 50000000L;
    }

    nanosleep((const struct timespec[]){ { sleep_time_s, sleep_time_n } },
              NULL);
  }

  return NULL;
}

/* ====================== */
/* Master side of the PTY */
/* ====================== */
void
master(int fd_master, int fd_slave, int fdl, int fdc)
{

  pthread_t t1;
  pthread_t t2;

  struct args_s args1 = { fd_master, fdl };
  struct args_s args2 = { fd_slave, fdc };

  init_etime();

  pthread_create(&t1, NULL, manage_io, &args1);
  pthread_create(&t2, NULL, inject_keys, &args2);

  pthread_join(t2, NULL);

  close(fdc);
  close(fdl);
}

/* ===================== */
/* Slave side of the PTY */
/* ===================== */
void
slave(int fd_slave, char ** argv)
{
  int rc;

  /* The slave side of the PTY becomes the standard input and outputs */
  /* of the child process.                                            */
  /* """""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
  close(0); /* Close standard input (current terminal)  */
  close(1); /* Close standard output (current terminal) */
  close(2); /* Close standard error (current terminal)  */

  /* PTY becomes standard input (0) */
  /* """""""""""""""""""""""""""""" */
  if (dup(fd_slave) == -1)
    msg(FATAL, "Error %d on dup()", errno);

  /* PTY becomes standard output (1) */
  /* """"""""""""""""""""""""""""""" */
  if (dup(fd_slave) == -1)
    msg(FATAL, "Error %d on dup()", errno);

  /* PTY becomes standard error (2) */
  /* """""""""""""""""""""""""""""" */
  if (dup(fd_slave) == -1)
    msg(FATAL, "Error %d on dup()", errno);

  /* Make the current process a new session leader */
  /* """"""""""""""""""""""""""""""""""""""""""""" */
  setsid();

  /* As the child is a session leader, set the controlling terminal to */
  /* be the slave side of the PTY (Mandatory for programs like thea    */
  /* shell to make them correctly manage their outputs).               */
  /* """"""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
  ioctl(0, TIOCSCTTY, 1);

  /* Set the effective uid from the real uid. */
  /* """""""""""""""""""""""""""""""""""""""" */
  seteuid(getuid());

  setupterm((char *)0, 1, (int *)0);

  /* Program execution with its arguments */
  /* """""""""""""""""""""""""""""""""""" */
  rc = execvp(argv[my_optind], argv + my_optind);
  if (rc == -1)
    msg(FATAL, "Error %d on execvp()", errno);
}

/* ================================= */
/* Print message about a bad option. */
/* ================================= */
int
badopt(const char * mess, int ch)
{
  if (my_opterr)
  {
    fputs(prog, stderr);
    fputs(mess, stderr);
    (void)putc(ch, stderr);
    (void)putc('\n', stderr);
  }
  return ('?');
}

/* ========================================================================== */
/* my_getopt - get option letter from argv                                    */
/*                                                                            */
/* This is a version of the public domain getopt() implementation by          */
/* Henry Spencer, changed for 4.3BSD compatibility (in addition to System V). */
/* It allows rescanning of an option list by setting optind to 0 before       */
/* calling, which is why we use it even if the system has its own (in fact,   */
/* this one has a unique name so as not to conflict with the system's).       */
/* Thanks to Dennis Ferguson for the appropriate modifications.               */
/*                                                                            */
/* This file is in the Public Domain.                                         */
/* ========================================================================== */
int
my_getopt(int argc, char * argv[], const char * optstring)
{
  register char         c;
  register const char * place;

  my_optarg = NULL;

  if (my_optind == 0)
  {
    scan = NULL;
    my_optind++;
  }

  if (scan == NULL || *scan == '\0')
  {
    if (my_optind >= argc || argv[my_optind][0] != '-'
        || argv[my_optind][1] == '\0')
    {
      return (EOF);
    }
    if (argv[my_optind][1] == '-' && argv[my_optind][2] == '\0')
    {
      my_optind++;
      return (EOF);
    }

    scan = argv[my_optind++] + 1;
  }

  c         = *scan++;
  my_optopt = c & 0377;
  for (place = optstring; place != NULL && *place != '\0'; ++place)
    if (*place == c)
      break;

  if (place == NULL || *place == '\0' || c == ':' || c == '?')
  {
    return (badopt(": unknown option -", c));
  }

  place++;
  if (*place == ':')
  {
    if (*scan != '\0')
    {
      my_optarg = scan;
      scan      = NULL;
    }
    else if (my_optind >= argc)
    {
      return (badopt(": option requires argument -", c));
    }
    else
    {
      my_optarg = argv[my_optind++];
    }
  }

  return (c & 0377);
}

/* ============= */
/* program entry */
/* ============= */
int
main(int argc, char * argv[])
{
  int      fd_master, fd_slave;
  int      opt;
  int      fdl, fdc = 0;
  unsigned n;
  int      end;
  unsigned width  = 0;
  unsigned height = 0;
  pid_t    slave_pid;

  duration = default_duration;

  while ((opt = my_getopt(argc, argv, "Vl:s:i:w:h:d:o:")) != -1)
  {
    switch (opt)
    {
      case 'V':
        fprintf(stderr, "Version: " VERSION "\n");
        exit(0);

      case 'l':
        log_file = strdup(my_optarg);
        break;

      case 's':
        srt_file = strdup(my_optarg);
        break;

      case 'i':
        fdc = open(my_optarg, O_RDONLY);
        if (fdc == -1)
        {
          msg(WARN, "Cannot open %s\n", my_optarg);
          usage(argv[0]);
        }
        break;

      case 'd':
        duration = atoi(my_optarg);
        if (duration <= 0)
          duration = default_duration;
        break;

      case 'o':
        offset = atol(my_optarg);
        if (offset < 0)
        {
          offset      = -offset;
          offset_sign = -1;
        }
        srt_offset.tv_sec  = offset / 1000;
        srt_offset.tv_usec = (offset - srt_offset.tv_sec * 1000) * 1000;

        break;

      case 'w':
        n = sscanf(my_optarg, "%u%n", &width, &end);
        if (n != 1 || my_optarg[end] != '\0')
          usage(argv[0]);
        if (width == 0)
          usage(argv[0]);
        break;

      case 'h':
        n = sscanf(my_optarg, "%u%n", &height, &end);
        if (n != 1 || my_optarg[end] != '\0')
          usage(argv[0]);
        if (height == 0)
          usage(argv[0]);
        break;

      default:
        usage(argv[0]);
    }
  }

  if (optind >= argc)
  {
    msg(WARN, "Expected argument after options\n");
    usage(argv[0]);
    exit(EXIT_FAILURE);
  }

  /* Manage the arguments list */
  /* """"""""""""""""""""""""" */
  if (argc <= 1)
    msg(FATAL, "Usage: %s program_name [parameters]", argv[0]);

  if (fdc == -1)
    usage(argv[0]);

  if (log_file == NULL)
    log_file = "ptylog";

  if (srt_file == NULL)
    srt_file = "ptylog.srt";

  fdl = open(log_file, O_RDWR | O_CREAT | O_TRUNC,
             S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);

  chown(log_file, getuid(), getgid());

  fd_master = open_master();

  /* Open the slave side of the PTY */
  /* """""""""""""""""""""""""""""" */
  fd_slave = open(ptsname(fd_master), O_RDWR | O_APPEND);

  /* Initialize the terminal */
  /* """"""""""""""""""""""" */
  set_terminal(fd_master, &old_termios_master);
  set_terminal(fd_slave, &old_termios_slave);

  /* Restore terminal on exit */
  /* """""""""""""""""""""""" */
  atexit(cleanup);

  set_terminal_size(fd_slave, width, height);

  /* Create the child process */
  /* """""""""""""""""""""""" */
  if ((slave_pid = fork()))
  {
    int rc;

    master(fd_master, fd_slave, fdl, fdc);

    /* Wait for the slave to end */
    /* ''''''''''''''''''''''''' */
    wait(&rc);
  }
  else
    slave(fd_slave, argv);

  return 0;
}
