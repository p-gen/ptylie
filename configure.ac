#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

m4_define(my_version, [m4_esyscmd([tr -d '\n' < version])])

AC_PREREQ([2.69])
AC_INIT([ptylie], [my_version], [BUG-REPORT-ADDRESS])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([no-dist-gzip dist-bzip2 foreign -Wall])
AM_SILENT_RULES([yes])


# Checks for programs.
AC_PROG_CC
AM_PROG_CC_C_O

# Checks for libraries.
AC_CHECK_LIB([pthread], [pthread_create])
AC_SEARCH_LIBS([tgetent], [tinfo curses ncursesw ncurses], [HAVE_CURSES=True])

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h stdlib.h string.h sys/ioctl.h sys/time.h termios.h unistd.h utmp.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T

# Checks for library functions.
AC_FUNC_CHOWN
AC_FUNC_ERROR_AT_LINE
AC_FUNC_FORK
AC_FUNC_MALLOC
AC_CHECK_FUNCS([atexit memset putenv select strdup strtol])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT([ptylie.spec])
