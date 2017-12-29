#define _XOPEN_SOURCE 600
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
#include <sys/select.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

typedef struct stk_s stk_t;

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

int
main(int argc, char * argv[]);

/* ----------- */
/* Definitions */
/* ----------- */

/* Terminal settings backups */
/* """"""""""""""""""""""""" */
struct termios old_termios_master;
struct termios old_termios_slave;

int fd_termios;

char * my_optarg;     /* global argument pointer. */
int    my_optind = 0; /* global argv index. */
int    my_opterr = 1; /* for compatibility, should error be printed? */
int    my_optopt;     /* for compatibility, option character checked */

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

const char * prog = "ptylie";
char *       scan = NULL; /* Private scan pointer. */

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

/* ----------------- */
/* Utility functions */
/* ----------------- */

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
  struct winsize ws;

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
  unsigned char scanf_buf[4096];
  char          tmp[256];
  char          rows[4], cols[4];
  int           special      = 0;
  int           meta         = 0;
  int           control      = 0;
  long          sleep_time   = 0;
  time_t        sleep_time_s = 0;
  long          sleep_time_n = 0;

  stk_t fd_stack; /* int stack to remember nested file descriptors *
                   * used by the 'R' command                       */

  int fd  = ((struct args_s *)args)->fd1;
  int fdc = ((struct args_s *)args)->fd2;

  struct winsize ws;

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
          get_arg(fdc, scanf_buf, &len);
          n = sscanf((char *)scanf_buf, "[%5[0-9]]%n", tmp, &l);
          if (n != 1)
            exit(EXIT_FAILURE);

          if (c == 'S')
          {
            long   delay_ms = atol(tmp);       /* milliseconds */
            time_t s        = delay_ms / 1000; /* seconds */

            nanosleep(
              (const struct timespec[]){
                { s, delay_ms * 1000000L - s * 1000000000L } },
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
            if (scanf_buf[strlen(scanf_buf + 1)] == ']')
              scanf_buf[strlen(scanf_buf + 1)] = '\0';

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

        case 'u': /* for raw hexadecimal UTF-8 injection \u[xxyyzztt]*/
          get_arg(fdc, scanf_buf, &len);
          n = sscanf((char *)scanf_buf, "[%8[0-9a-fA-F]]%n", tmp, &l);
          if (n != 1)
            exit(EXIT_FAILURE);

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

    /* Inject a sequence of l (>=1) characters in one shot */
    /* ''''''''''''''''''''''''''''''''''''''''''''''''''' */
    if (l > 1)
    {
      tmp[1] = '\0';
      for (i = 0; i < l; i++)
      {
        tmp[0] = buf[i];

        if (ioctl(fd, TIOCSTI, tmp) < 0)
          exit(EXIT_FAILURE);
      }
    }
    else
    {
      if (ioctl(fd, TIOCSTI, buf) < 0)
        exit(EXIT_FAILURE);
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

  /* inter injection loop 1/20 s min to leave the application */
  /* the time to read the keyboard.                           */
  /* """""""""""""""""""""""""""""""""""""""""""""""""""""""" */
  loop:
    if (sleep_time > 50)
    {
      nanosleep((const struct timespec[]){ { sleep_time_s, sleep_time_n } },
                NULL);
    }
    else
      /* default to 1/20 s when sleep_time is set to 0 */
      /* ''''''''''''''''''''''''''''''''''''''''''''''' */
      nanosleep((const struct timespec[]){ { 0, 50000000L } }, NULL);
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
          msg(WARN, "Cannot open %s\n", my_optarg);
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

  fdl = open(log_file, O_RDWR | O_CREAT | O_TRUNC,
             S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);

  chown(log_file, getuid(), getgid());

  fd_master = open_master();

  /* Open the slave side of the PTY */
  /* """""""""""""""""""""""""""""" */
  fd_slave = open(ptsname(fd_master), O_RDWR);

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
