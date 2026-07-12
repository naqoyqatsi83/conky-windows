/* Windows compat layer for missing POSIX functions */
#ifndef COMPAT_WINDOWS_H
#define COMPAT_WINDOWS_H

#ifdef _WIN32
#define _USE_MATH_DEFINES 1
#include <stddef.h>
#include <sys/stat.h>

/* POSIX types not defined by MinGW */
#ifndef _UID_T_DEFINED
#define _UID_T_DEFINED
typedef unsigned int uid_t;
#endif
#ifndef _GID_T_DEFINED
#define _GID_T_DEFINED
typedef unsigned int gid_t;
#endif
#ifndef _IN_PORT_T_DEFINED
#define _IN_PORT_T_DEFINED
typedef unsigned short in_port_t;
#endif

/* Include compat pwd.h for getpwuid/getpwnam (needed by top.cc etc.) */
#include <pwd.h>

/* O_NONBLOCK - not available on Windows; files are always blocking */
#ifndef O_NONBLOCK
#define O_NONBLOCK 0
#endif

/* S_IFLNK / S_ISLNK - not defined by MinGW */
#ifndef S_IFLNK
#define S_IFLNK 0xA000
#endif
#ifndef S_ISLNK
#define S_ISLNK(m) (((m) & S_IFMT) == S_IFLNK)
#endif

#ifndef COMPAT_WINDOWS_IMPL
/* Function declarations available to the rest of the codebase */

#ifdef __cplusplus
extern "C" {
#endif

/* Provide strndup - POSIX 2008 */
char *compat_strndup(const char *s, size_t n);

/* Provide setenv/unsetenv - POSIX */
int compat_setenv(const char *name, const char *value, int overwrite);
int compat_unsetenv(const char *name);

/* Provide asprintf - GNU extension */
int compat_asprintf(char **strp, const char *fmt, ...);

/* Provide strcasestr - BSD/GNU */
char *compat_strcasestr(const char *haystack, const char *needle);

/* Provide readlink - POSIX */
int compat_readlink(const char *path, char *buf, size_t bufsiz);

/* Provide getloadavg - BSD */
int compat_getloadavg(double loadavg[], int nelem);

#ifdef __cplusplus
}
#endif

/* Redirect POSIX names to compat implementations */
#ifndef COMPAT_NO_REDIRECT
#define strndup(s, n) compat_strndup(s, n)
#define setenv(n, v, o) compat_setenv(n, v, o)
#define unsetenv(n) compat_unsetenv(n)
#define asprintf(s, f, ...) compat_asprintf(s, f, ##__VA_ARGS__)
#define strcasestr(h, n) compat_strcasestr(h, n)
#define readlink(p, b, s) compat_readlink(p, b, s)
#define getloadavg(l, n) compat_getloadavg(l, n)
#endif /* COMPAT_NO_REDIRECT */

#endif /* COMPAT_WINDOWS_IMPL */
#endif /* _WIN32 */

#endif /* COMPAT_WINDOWS_H */
