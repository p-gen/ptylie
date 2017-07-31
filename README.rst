Description
===========
Start a program in a pseudo terminal and:

- redirect its input from stdin, and output to stdout
- log everything in a log file name *ptylog* by default
- inject keystrokes and other events according to directives given in
  a file

|
| The PTY part of this programm is: 
| Originally by Rachid Koucha.
| Enhanced by Lars Brinhoff and Pierre Gentile for use in this program.
|
| http://rachid.koucha.free.fr/tech_corner/pty_pdip.html
| https://github.com/larsbrinkhoff/pty-stdio

Building
========
As git does not store the timestamps of the files it handles, I encourage
you to use the provided *build.sh* script to build this program.

Use it as you'd use for *configure*.

Synopsis
========
| ``ptylie [-l log_file]``
|          ``[-w terminal_width]``
|          ``[-h terminal_height]``
|          ``[-i command_file]``
|          ``program_to_launch program_arguments``

command_file
============
Each character present in this file will be injected into the keyboard
buffer of *program_to_launch*.
Legal C special characters like ``\t`` and ``\b`` are understood.

Some extra special characters are also understood:

:``\s[n]``:
    sets an interval of *n* ms between each of the next characters
    to inject.
:``\S[n]``:
    sleeps once for *n* ms.
:``\u[h]``:
    injects a sequence of up to 4 hexadecimal numbers (UTF-8).
:``\W[XxY]``:
    resizes the slave's terminal to *X* columns and *Y* lines.

IMPORTANT NOTE
==============
The *command_file* must start with a ``\s`` directive for this program
to work.

IMPORTANT SECURTITY CONSIDERATION
=================================
The keystrokes injection mechanism only work if the binary is installed
belonging to root or with the setuid bit set.
