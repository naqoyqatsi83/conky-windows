/* Minimal compat stub for netdb.h on Windows/MinGW */
#ifndef _COMPAT_NETDB_H
#define _COMPAT_NETDB_H

#include <winsock2.h>
#include <ws2tcpip.h>

/* Addrinfo constants */
#ifndef AI_PASSIVE
#define AI_PASSIVE 1
#define AI_CANONNAME 2
#define AI_NUMERICHOST 4
#define AI_NUMERICSERV 8
#define AI_V4MAPPED 0x0008
#define AI_ALL 0x0100
#define AI_ADDRCONFIG 0x0400
#endif

#ifndef NI_NUMERICHOST
#define NI_NUMERICHOST 1
#define NI_NUMERICSERV 2
#define NI_NOFQDN 4
#define NI_NAMEREQD 8
#define NI_DGRAM 16
#define NI_MAXHOST 1025
#define NI_MAXSERV 32
#endif

/* Note: struct servent, getservbyname, getservbyport are provided by Winsock2 */

#endif /* _COMPAT_NETDB_H */
