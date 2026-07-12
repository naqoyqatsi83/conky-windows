/* Minimal compat stub for termios.h on Windows/MinGW */
#ifndef _COMPAT_TERMIOS_H
#define _COMPAT_TERMIOS_H

/* termios is not available on Windows; provide stub macros */
#define B9600 0
#define CS8 0
#define CLOCAL 0
#define CREAD 0
#define IGNPAR 0
#define TCSANOW 0
#define ECHO 8
#define ICANON 2
#define VMIN 0
#define VTIME 0

struct termios {
  int c_iflag;
  int c_oflag;
  int c_cflag;
  int c_lflag;
};

static inline int tcgetattr(int fd, struct termios *t) {
  (void)fd; (void)t; return -1;
}

static inline int tcsetattr(int fd, int opt, const struct termios *t) {
  (void)fd; (void)opt; (void)t; return -1;
}

#endif /* _COMPAT_TERMIOS_H */
