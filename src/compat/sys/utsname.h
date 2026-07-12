/* Compat sys/utsname.h for Windows/MinGW */
#ifndef _COMPAT_SYS_UTSNAME_H
#define _COMPAT_SYS_UTSNAME_H

#ifdef __cplusplus
extern "C" {
#endif

#define _UTSNAME_LENGTH 256

struct utsname {
  char sysname[_UTSNAME_LENGTH];
  char nodename[_UTSNAME_LENGTH];
  char release[_UTSNAME_LENGTH];
  char version[_UTSNAME_LENGTH];
  char machine[_UTSNAME_LENGTH];
};

int uname(struct utsname *__buf);

#ifdef __cplusplus
}
#endif

#endif /* _COMPAT_SYS_UTSNAME_H */
