/* Minimal compat stub for grp.h on Windows/MinGW */
#ifndef _COMPAT_GRP_H
#define _COMPAT_GRP_H

#include <stdlib.h>
#include <string.h>

struct group {
  char *gr_name;
  char *gr_passwd;
  int gr_gid;
  char **gr_mem;
};

static inline struct group *getgrgid(int gid) { (void)gid; return NULL; }
static inline struct group *getgrnam(const char *name) { (void)name; return NULL; }

#endif /* _COMPAT_GRP_H */
