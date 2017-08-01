#define _XOPEN_SOURCE 600
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <termios.h>
#include <unistd.h>

static void
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

static struct termios old_termios;
static int            fd_termios;

char * my_optarg;     /* Global argument pointer. */
int    my_optind = 0; /* Global argv index. */
int    my_opterr = 1; /* for compatibility, should error be printed? */
int    my_optopt;     /* for compatibility, option character checked */

struct args_s
{
  int fd1;
  int fd2;
};

static const char * prog = "ptylie";
static char *       scan = NULL; /* Private scan pointer. */

static void
cleanup(void)
{
  tcsetattr(fd_termios, TCSANOW, &old_termios);
}

static void
handler(int sig)
{
  exit(0);
}

static void
fatal(const char * message, ...)
{
  va_list args;

  va_start(args, message);
  vfprintf(stderr, message, args);
  fputc('\n', stderr);
  va_end(args);

  exit(1);
}

/* =========================================================== */
/* Sets the the terminal fd in raw mode, returns -& on failure */
/* =========================================================== */
static int
tty_raw(struct termios * attr, int fd)
{
  attr->c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
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
static int
open_master(void)
{
  int rc, fd_master;

  fd_master = posix_openpt(O_RDWR);
  if (fd_master < 0)
    fatal("Error %d on posix_openpt()", errno);

  rc = grantpt(fd_master);
  if (rc != 0)
    fatal("Error %d on grantpt()", errno);

  rc = unlockpt(fd_master);
  if (rc != 0)
    fatal("Error %d on unlockpt()", errno);

  return fd_master;
}

/* ======================================================= */
/* Utility function that Reads bytes from in an write them */
/* to out and log                                          */
/* ======================================================= */
static void
read_write(const char * name, int in, int out, int log)
{
  char input[128];
  int  rc;

  rc = read(in, input, sizeof input);
  if (rc < 0)
  {
    if (errno == EIO)
      exit(0);

    fatal("Error %d on read %s", errno, name);
  }

  write(out, input, rc);
  write(log, input, rc);
}

/* ======================= */
/* Terminal initialization */
/* ======================= */
static void
set_terminal(int fd_master, unsigned width, unsigned height)
{
  struct termios   new_termios;
  struct winsize   ws;
  int              rc;
  struct sigaction sa;

  if (isatty(0))
    fd_termios = 0;
  else if (isatty(1))
    fd_termios = 1;
  else
    return;

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
  ioctl(fd_master, TIOCSWINSZ, &ws);

  /* Save the defaults parameters to be able to restore then on exit */
  /* and in case of reception of an INT signal.                      */
  rc = tcgetattr(fd_termios, &old_termios);
  if (rc == -1)
    fatal("Error %d on tcgetattr()", errno);

  /* Restore terminal on exit */
  /* """""""""""""""""""""""" */
  atexit(cleanup);
  siginterrupt(SIGINT, 1);

  /* Setup the sighub handler */
  /* """""""""""""""""""""""" */
  sa.sa_handler = handler;
  sa.sa_flags   = 0;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGINT, &sa, NULL);

  if (fd_termios == 0)
  {
    /* Set RAW mode on stdin */
    /* """"""""""""""""""""" */
    new_termios = old_termios;
    if (tty_raw(&new_termios, 0) == -1)
      fatal("Cannot set fd 0 in raw mode");
  }
}

/* ================================================================= */
/* This function is responsible to send and receive io in the master */
/* part. The hard work is done by the read_write function.           */
/* ================================================================= */
static void *
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
      fatal("Error %d on select()", errno);

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

/* ================================================================= */
/* Injects keys in the slave's keyboard buffer, we need to have root */
/* privileges to do that.                                            */
/* Manages special dirsectives introduces by \s, \S, \W and \u       */
/* ================================================================= */
static void *
inject_keys(void * args)
{
  int           data;
  int           rc;
  int           i;
  int           l, len;
  int           n;
  unsigned char c;
  unsigned char buf[4096];
  unsigned char scanf_buf[4096];
  char          tmp[256];
  char          rows[4], cols[4];
  int           special    = 0;
  int           meta       = 0;
  int           control    = 0;
  long          sleep_time = 0;

  int fd  = ((struct args_s *)args)->fd1;
  int fdc = ((struct args_s *)args)->fd2;

  struct winsize ws;

  for (;;)
  {
    rc = read(fdc, &data, 1);
    if (rc == -1 || rc == 0)
      break;

    c = (unsigned char)data;

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
        l      = 2;
      }
      else if (control)
      {
        control = 0;
        buf[0]  = toupper(c) - '@';
        l       = 1;
      }
      else
      {
        buf[0] = c;
        l      = 1;
      }
    }
    else
    {
      l       = 1;
      special = 0;
      switch (c)
      {
        case '\n':
          goto loop;

        case '\\':
          buf[0] = '\\';
          break;

        case 's': /* set new seep time between keytrokes        */
        case 'S': /* sleep for the given amount of milliseconds */
          len  = 0;
          rc   = 0;
          data = '\0';
          while (rc != -1 && data != ']' && len < 255)
          {
            rc = read(fdc, &data, 1);
            if (rc == -1 || rc == 0)
              break;
            scanf_buf[len++] = (unsigned char)data;
          }
          scanf_buf[len] = '\0';
          n              = sscanf((char *)scanf_buf, "[%5[0-9]]%n", tmp, &l);
          if (n != 1)
            exit(1);
          if (c == 'S')
          {
            usleep(atoi(tmp) * 1000);
            continue;
          }
          else
          {
            sleep_time = atoi(tmp) * 1000;
            goto loop;
          }

        case 'W': /* for terminal resizing (ex: [80x24] */
          len  = 0;
          rc   = 0;
          data = '\0';
          while (rc != -1 && data != ']' && len < 255)
          {
            rc = read(fdc, &data, 1);
            if (rc == -1 || rc == 0)
              break;
            scanf_buf[len++] = (unsigned char)data;
          }
          scanf_buf[len] = '\0';
          n = sscanf((char *)scanf_buf, "[%3[0-9]x%3[0-9]]%n", cols, rows, &l);
          if (n != 2)
            exit(1);
          memset(&ws, 0, sizeof ws);
          ws.ws_row = atoi(rows);
          ws.ws_col = atoi(cols);
          ioctl(fd, TIOCSWINSZ, &ws);
          goto loop;

        case 'u': /* for raw hexadecimal UTF-8 injection */
          len  = 0;
          rc   = 0;
          data = '\0';
          while (rc != -1 && data != ']' && len < 255)
          {
            rc = read(fdc, &data, 1);
            if (rc == -1 || rc == 0)
              break;
            scanf_buf[len++] = (unsigned char)data;
          }
          scanf_buf[len] = '\0';
          n = sscanf((char *)scanf_buf, "[%8[0-9a-fA-F]]%n", tmp, &l);
          if (n != 1)
            exit(1);

          for (i = 0; i < (l - 2) / 2; i++)
          {
            unsigned char charhex[3] = { tmp[i * 2], tmp[i * 2 + 1], 0 };
            tmp[i] = (unsigned char)strtol((char *)charhex, NULL, 16);
          }
          l      = (l - 2) / 2;
          tmp[l] = '\0';
          memcpy(buf, tmp, l);
          break;

        case 'c': /* colour setting \c[x;y;z] */
          buf[0] = 0x1b;
          buf[1] = '[';
          l      = 2;
          len    = 0;
          rc     = 0;
          data   = '\0';
          while (rc != -1 && data != ']' && len < 255)
          {
            rc = read(fdc, &data, 1);
            if (rc == -1 || rc == 0)
              break;
            scanf_buf[len++] = (unsigned char)data;
          }
          scanf_buf[len] = '\0';
          n              = sscanf((char *)scanf_buf, "[%8[0-9;]]", tmp);
          if (n != 1)
            exit(1);

          while (l < len)
          {
            buf[l] = tmp[l - 2];
            l++;
          }
          buf[l] = 'm';
          l++;
          break;

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
          break;

        case '\'':
          buf[0] = '\'';
          break;

        case 'a':
          buf[0] = '?';
          break;

        case 't':
          buf[0] = '\t';
          break;

        case 'n':
          buf[0] = '\0';
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

    /* close the buffer and inject it */
    /* """""""""""""""""""""""""""""" */
    buf[l] = '\0';
    if (l > 1)
    {
      tmp[1] = '\0';
      for (i = 0; i < l; i++)
      {
        tmp[0] = buf[i];
        if (ioctl(fd, TIOCSTI, tmp) < 0)
          exit(1);
      }
    }
    else
    {
      if (ioctl(fd, TIOCSTI, buf) < 0)
        exit(1);
    }

  /* inter injection loop */
  /* """""""""""""""""""" */
  loop:
    if (sleep_time > 0)
      usleep(sleep_time);
  }

  return NULL;
}

/* ====================== */
/* Master side of the PTY */
/* ====================== */
static void
master(int fd_master, int fd_slave, int fdl, int fdc)
{

  pthread_t t1;
  pthread_t t2;

  struct args_s args1 = { fd_master, fdl };
  struct args_s args2 = { fd_slave, fdc };

  pthread_create(&t1, NULL, manage_io, &args1);
  pthread_create(&t2, NULL, inject_keys, &args2);

  pthread_join(t2, NULL);

  close(fdc);
  close(fdl);
}

/* ===================== */
/* Slave side of the PTY */
/* ===================== */
static void
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
    fatal("Error %d on dup()", errno);

  /* PTY becomes standard output (1) */
  /* """"""""""""""""""""""""""""""" */
  if (dup(fd_slave) == -1)
    fatal("Error %d on dup()", errno);

  /* PTY becomes standard error (2) */
  /* """""""""""""""""""""""""""""" */
  if (dup(fd_slave) == -1)
    fatal("Error %d on dup()", errno);

  /* Make the current process a new session leader */
  /* """"""""""""""""""""""""""""""""""""""""""""" */
  setsid();

  /* As the child is a session leader, set the controlling terminal to */
  /* be the slave side of the PTY (Mandatory for programs like thea    */
  /* shell to make them correctly manage their outputs).               */
  /* """"""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
  ioctl(0, TIOCSCTTY, 1);

  /* Execution of the program */
  /* """""""""""""""""""""""" */
  rc = execvp(argv[my_optind], argv + my_optind);
  if (rc == -1)
    fatal("Error %d on execvp()", errno);
}

/* ================================= */
/* Print message about a bad option. */
/* ================================= */
static int
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
static int
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

  char * log_file = NULL;

  while ((opt = my_getopt(argc, argv, "l:i:w:h:")) != -1)
  {
    switch (opt)
    {
      case 'l':
        log_file = strdup(my_optarg);
        break;

      case 'i':
        fdc = open(my_optarg, O_RDONLY);
        if (fdc == -1)
        {
          fprintf(stderr, "Cannot open %s\n", my_optarg);
          usage(argv[0]);
        }
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
    fprintf(stderr, "Expected argument after options\n");
    usage(argv[0]);
    exit(EXIT_FAILURE);
  }

  /* Manage the arguments list */
  /* """"""""""""""""""""""""" */
  if (argc <= 1)
    fatal("Usage: %s program_name [parameters]", argv[0]);

  if (fdc == -1)
    usage(argv[0]);

  if (log_file == NULL)
    log_file = "ptylog";

  fdl = open(log_file, O_RDWR | O_CREAT | O_TRUNC,
             S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);

  chown(log_file, getuid(), getgid());

  fd_master = open_master();

  /* Open the slave side of the PTY */
  /* """""""""""""""""""""""""""""" */
  fd_slave = open(ptsname(fd_master), O_RDWR);

  /* Initialize the terminal */
  /* """"""""""""""""""""""" */
  set_terminal(fd_slave, width, height);

  /* Create the child process */
  /* """""""""""""""""""""""" */
  if (fork())
    master(fd_master, fd_slave, fdl, fdc);
  else
    slave(fd_slave, argv);

  return 0;
}
