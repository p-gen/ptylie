Start a program in a pseudo terminal and:

- redirect its input from stdin, and output to stdout
- log everything in a log file name *ptylog* by default
- inject keystrokes and other events according to directives given in a file

|
| The PTY part of this programm is: 
| Originally by Rachid Koucha.
| Enhanced by Lars Brinhoff and Pierre Gentile for use in this program.
|
| http://rachid.koucha.free.fr/tech_corner/pty_pdip.html
| https://github.com/larsbrinkhoff/pty-stdio
