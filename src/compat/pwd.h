/* Minimal compat stub for pwd.h on Windows/MinGW */
#ifndef _COMPAT_PWD_H
#define _COMPAT_PWD_H

#include <stdlib.h>
#include <string.h>

struct passwd {
  char *pw_name;
  char *pw_passwd;
  int pw_uid;
  int pw_gid;
  char *pw_gecos;
  char *pw_dir;
  char *pw_shell;
};

static inline struct passwd *getpwuid(int uid) { (void)uid; return NULL; }
static inline struct passwd *getpwnam(const char *name) { (void)name; return NULL; }

#endif /* _COMPAT_PWD_H */
