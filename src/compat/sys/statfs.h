/* Minimal compat stub for sys/statfs.h on Windows/MinGW */
#ifndef _COMPAT_SYS_STATFS_H
#define _COMPAT_SYS_STATFS_H

struct statfs {
  long f_type;
  long f_bsize;
  long f_blocks;
  long f_bfree;
  long f_bavail;
  long f_files;
  long f_ffree;
  long f_fsid;
  long f_namelen;
  long f_frsize;
  long f_flags;
  long f_spare[4];
};

#endif /* _COMPAT_SYS_STATFS_H */
