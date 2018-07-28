.. image:: ptylie.gif

Description
-----------
Start a program in a pseudo terminal and:

- redirect its input from stdin, and output to stdout
- log everything in a log file named *ptylog* by default
- inject keystrokes and other events according to directives given in
  a file

|

The PTY part of this programm is originally by Rachid Koucha 
enhanced by Lars Brinhoff and Pierre Gentile for use in this program.

| http://rachid.koucha.free.fr/tech_corner/pty_pdip.html
| https://github.com/larsbrinkhoff/pty-stdio

|

If you want to see another demonstration, just look at the screencast
in the README of my ``smenu`` utility here: https://github.com/p-gen/smenu

IMPORTANT - PLEASE READ
-----------------------
The keystrokes injection mechanism **only works** if the binary is
running with root privilege.

The two main ways to achieve this are:

- by setting the setuid bit in the executable's permissions (ex:
  ``chmod 4755``).
- by running the programme with ``sudo`` or an equivalent program.

|

``make install`` will produce an executable with the setuid bit set. If
that's not what you want, please unset this bit (ex: ``chmod 755``)
and use ``sudo``.

Note that if you do this and use ``sudo``, then the program started by
``ptylie`` will work as root and not with the user's account.

Building
--------
As git does not store the timestamps of the files it handles, I encourage
you to use the provided *build.sh* script to build this program.

Use it as you would with configure.

Synopsis
--------
| ``ptylie [-V] [-l log_file] [-w terminal_width] [-h terminal_height]``
| ``[-i command_file] program_to_launch program_arguments``

command_file
------------
Each character present in this file will be injected into the keyboard
buffer of *program_to_launch*.
Legal C special characters like ``\t`` and ``\b`` are understood.

Some extra special sequences are also understood:

:``\s[n]``:
    sets an interval of **n** ms between each of the next characters
    to inject.

    When not set or set to a value less than 1/20s, the interval is 1/20s.
:``\S[n]``:
    sleeps once for **n** ms.
:``\u[hh...]``:
    injects a sequence of up to 4 hexadecimal numbers (UTF-8).
:``\x[hh...]``:
    injects a sequence of up to 128 characters (each coded on 2
    hexadecimal positions).
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
:``\m[file]``:
    use another map file for the subtitles, see below.
:``\k``:
    active/deactivate the subtitle generation

