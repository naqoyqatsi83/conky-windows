/* Minimal compat stub for sys/ioctl.h on Windows/MinGW */
#ifndef _COMPAT_SYS_IOCTL_H
#define _COMPAT_SYS_IOCTL_H

static inline int ioctl(int fd, unsigned long request, ...) {
  (void)fd; (void)request; return -1;
}

#endif /* _COMPAT_SYS_IOCTL_H */
