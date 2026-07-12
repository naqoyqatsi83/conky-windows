/* Compat redirect netinet/in.h -> winsock2 for Windows/MinGW */
#ifndef _COMPAT_NETINET_IN_H
#define _COMPAT_NETINET_IN_H

#include <winsock2.h>
#include <ws2tcpip.h>

#endif /* _COMPAT_NETINET_IN_H */
