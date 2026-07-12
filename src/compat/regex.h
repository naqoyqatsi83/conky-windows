/* Minimal compat stub for regex.h on Windows/MinGW */
#ifndef _COMPAT_REGEX_H
#define _COMPAT_REGEX_H

#include <stdlib.h>
#include <string.h>

typedef struct {
  size_t re_nsub;
} regex_t;

typedef size_t regoff_t;

typedef struct {
  regoff_t rm_so;
  regoff_t rm_eo;
} regmatch_t;

#define REG_EXTENDED 1
#define REG_ICASE 2
#define REG_NEWLINE 4
#define REG_NOSUB 8
#define REG_NOTBOL 1
#define REG_NOTEOL 2

#define REG_NOMATCH 1
#define REG_BADPAT 2
#define REG_ESPACE 3

static inline int regcomp(regex_t *preg, const char *regex, int cflags) {
  (void)regex;
  (void)cflags;
  preg->re_nsub = 0;
  return 0;
}

static inline int regexec(const regex_t *preg, const char *string,
                          size_t nmatch, regmatch_t pmatch[], int eflags) {
  (void)preg;
  (void)string;
  (void)nmatch;
  (void)pmatch;
  (void)eflags;
  return REG_NOMATCH;
}

static inline size_t regerror(int errcode, const regex_t *preg,
                              char *errbuf, size_t errbuf_size) {
  (void)errcode;
  (void)preg;
  if (errbuf_size > 0) errbuf[0] = '\0';
  return 0;
}

static inline void regfree(regex_t *preg) {
  (void)preg;
}

#endif /* _COMPAT_REGEX_H */
