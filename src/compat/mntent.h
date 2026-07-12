/* Minimal compat stub for mntent.h on Windows */
#ifndef _COMPAT_MNTENT_H
#define _COMPAT_MNTENT_H

struct mntent {
  char *mnt_fsname;
  char *mnt_dir;
  char *mnt_type;
  char *mnt_opts;
  int mnt_freq;
  int mnt_passno;
};

#define MOUNTED "/etc/mtab"

static inline struct mntent *getmntent(FILE *fp) { (void)fp; return NULL; }
static inline FILE *setmntent(const char *filename, const char *type) {
  (void)filename; (void)type; return NULL;
}
static inline int endmntent(FILE *fp) { (void)fp; return 1; }
static inline char *hasmntopt(const struct mntent *mnt, const char *opt) {
  (void)mnt; (void)opt; return NULL;
}

#endif /* _COMPAT_MNTENT_H */
