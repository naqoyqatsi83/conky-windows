/* Minimal compat stub for net/if.h on Windows/MinGW */
#ifndef _COMPAT_NET_IF_H
#define _COMPAT_NET_IF_H

#define IF_NAMESIZE 16
#define IFNAMSIZ IF_NAMESIZE

struct ifconf {
  int ifc_len;
  union {
    void *ifcu_buf;
  } ifc_ifcu;
};
#define ifc_buf ifc_ifcu.ifcu_buf

struct ifreq {
  char ifr_name[IFNAMSIZ];
};

#endif /* _COMPAT_NET_IF_H */
