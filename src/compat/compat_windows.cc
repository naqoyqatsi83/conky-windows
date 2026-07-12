/* Windows compat layer implementation for missing POSIX functions */
#define COMPAT_WINDOWS_IMPL
#define COMPAT_NO_REDIRECT
#include "compat_windows.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <windows.h>
#include <sys/utsname.h>

/* strndup - strdup with length limit */
char *compat_strndup(const char *s, size_t n) {
  if (s == NULL) return NULL;
  size_t len = strnlen(s, n);
  char *p = (char *)malloc(len + 1);
  if (p == NULL) return NULL;
  memcpy(p, s, len);
  p[len] = '\0';
  return p;
}

/* setenv - set environment variable */
int compat_setenv(const char *name, const char *value, int overwrite) {
  if (name == NULL || name[0] == '\0') { errno = EINVAL; return -1; }
  if (!overwrite) {
    char buf[4] = {0};
    DWORD ret = GetEnvironmentVariableA(name, buf, 0);
    if (ret > 0) return 0; /* already exists, don't overwrite */
  }
  if (SetEnvironmentVariableA(name, value)) return 0;
  errno = EINVAL;
  return -1;
}

/* unsetenv - unset environment variable */
int compat_unsetenv(const char *name) {
  if (name == NULL || name[0] == '\0') { errno = EINVAL; return -1; }
  if (SetEnvironmentVariableA(name, NULL)) return 0;
  errno = EINVAL;
  return -1;
}

/* asprintf - allocate and print to string */
int compat_asprintf(char **strp, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int len = _vscprintf(fmt, ap);
  va_end(ap);
  if (len < 0) { *strp = NULL; return -1; }
  *strp = (char *)malloc((size_t)len + 1);
  if (*strp == NULL) return -1;
  va_start(ap, fmt);
  int ret = vsnprintf(*strp, (size_t)len + 1, fmt, ap);
  va_end(ap);
  return ret;
}

/* strcasestr - case-insensitive string search */
char *compat_strcasestr(const char *haystack, const char *needle) {
  if (haystack == NULL || needle == NULL) return NULL;
  size_t needle_len = strlen(needle);
  if (needle_len == 0) return (char *)haystack;
  while (*haystack) {
    if (_strnicmp(haystack, needle, needle_len) == 0)
      return (char *)haystack;
    haystack++;
  }
  return NULL;
}

/* readlink - read symlink target (Windows doesn't have real symlinks like POSIX) */
int compat_readlink(const char *path, char *buf, size_t bufsiz) {
  (void)path; (void)buf; (void)bufsiz;
  errno = EINVAL;
  return -1;
}

/* getloadavg - load average (not meaningful on Windows) */
int compat_getloadavg(double loadavg[], int nelem) {
  if (nelem > 0) loadavg[0] = 0.0;
  if (nelem > 1) loadavg[1] = 0.0;
  if (nelem > 2) loadavg[2] = 0.0;
  return nelem;
}

/* uname - get system information (Windows implementation) */
int uname(struct utsname *buf) {
  if (buf == NULL) { errno = EFAULT; return -1; }
  memset(buf, 0, sizeof(*buf));

  strcpy(buf->sysname, "Windows");
  strcpy(buf->release, "10.0");
  strcpy(buf->version, "0");
  strcpy(buf->machine, "x86_64");

  DWORD size = sizeof(buf->nodename);
  if (!GetComputerNameExA(ComputerNameDnsHostname, buf->nodename, &size)) {
    strcpy(buf->nodename, "unknown");
  }
  return 0;
}
