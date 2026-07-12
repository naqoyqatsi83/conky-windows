/* Minimal compat stub for sys/wait.h on Windows/MinGW */
#ifndef _COMPAT_SYS_WAIT_H
#define _COMPAT_SYS_WAIT_H

#define WNOHANG 1
#define WUNTRACED 2
#define WEXITSTATUS(s) (((s) & 0xff00) >> 8)
#define WIFEXITED(s) (((s) & 0x7f) == 0)
#define WIFSIGNALED(s) ((((s) & 0x7f) > 0) && (((s) & 0x7f) < 0x7f))
#define WTERMSIG(s) ((s) & 0x7f)

#endif /* _COMPAT_SYS_WAIT_H */
