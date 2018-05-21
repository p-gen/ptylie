======
ptylie
======

-------------------------------------------------------
Record a terminal session with automatic keyboard entry
-------------------------------------------------------

:Author: p.gen.progs@gmail.com
:Copyright: Pierre Gentile (2018)
:Version: 0.1
:Manual section: 1
:Manual group: text processing

SYNOPSIS
========
| ``ptylie [-V] [-l log_file] [-w terminal_width] [-h terminal_height]``
| ``[-i command_file] program_to_launch program_arguments``


Description
===========
Start a program in a pseudo terminal and:

- redirect its input from stdin, and output to stdout
- log everything in a log file named *ptylog* by default
- inject keystrokes and other events according to directives given in
  a file

The PTY part of this programm is originally by Rachid Koucha
enhanced by Lars Brinhoff and Pierre Gentile for use in this program.

| http://rachid.koucha.free.fr/tech_corner/pty_pdip.html
| https://github.com/larsbrinkhoff/pty-stdio

If you want to see another demonstration, just look at the screencast
in the README of my ``smenu`` utility here: https://github.com/p-gen/smenu

Commands
========
Each character present in *command_file* will be injected into the
keyboard buffer of *program_to_launch*.

Legal C special characters like ``\t`` and ``\b`` are understood.

Some extra special sequences are also understood:

:``\s[n]``:
    sets an interval of **n** ms between each of the next characters
    to inject.

    When not set or set to a value less than 1/20s, the interval is 1/20s.
:``\S[n]``:
    sleeps once for **n** ms.
:``\u[h]``:
    injects a sequence of up to 4 hexadecimal numbers (UTF-8).
:``\W[XxY]``:
    resizes the slave's terminal to **X** columns and **Y** lines.
:``\c[color sequence]``:
    injects color codes, the trailing **m** is automatically added.
    (Ex: **33;1** injects the sequence ``\e[33;1m`` which means yellow).
    Use ``man console_codes`` for details.
:``\R[file]``:
    inserts the directives found in the file **file** from now. At end
    of file, switch back to the previous included file or the default
    command file.
:``\Cc``:
    injects the character ``CTRL-c`` where **c** is any character.
    The result is the same whether the character is lowercase or
    uppercase.
:``\Mc``:
    injects the character **c** preceded by an escape (0x1b).
    This sequence is generated when the ALT key is used.

