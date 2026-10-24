# Cygwin bug report: shell from system()/popen() missing from ps -ef

Reported to the Cygwin mailing list on 2026-10-24 (Cygwin 3.6.11-1,
Windows 10 build 19045).

Thread: https://inbox.sourceware.org/cygwin/CACWrzRKzkGLAiNrEw+aSwSgHujE-uq--Mi8mjdoLYEbJbOzMVw@mail.gmail.com/T/#u

- sent-message.txt : the message as sent
- send/            : the three attachments exactly as sent
                     (username and hostname replaced with USER and HOST)
- pscheck.c        : working copy of the test program

Build:  gcc pscheck.c -o pscheck -std=c90 -D_POSIX_C_SOURCE=200809L -Wall -Wpedantic
Run:    ./pscheck system | popen | fork | spawn
