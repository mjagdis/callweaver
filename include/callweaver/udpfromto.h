#ifndef UDPFROMTO_H
#define UDPFROMTO_H

#include <sys/socket.h>

extern CW_API_PUBLIC int cw_udpfromto_init(int s, int family);

extern CW_API_PUBLIC int cw_recvfromto(int s, void *buf, size_t len, int flags, struct sockaddr *from, socklen_t *fromlen, struct sockaddr *to, socklen_t *tolen);
extern CW_API_PUBLIC int cw_sendfromto(int s, const void *buf, size_t len, int flags, const struct sockaddr *from, socklen_t fromlen, const struct sockaddr *to, socklen_t tolen);

#endif
