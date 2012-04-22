#ifndef TCP_H_
#define TCP_H_

#include <sys/types.h>  /* socket, getaddrinfo */
#include <sys/socket.h> /* socket, getaddrinfo */
#include <netdb.h>      /* getaddrinfo */

int tcp_resolve_listen(
		char const *node,
		char const *service,
		struct addrinfo **res);

#define tcp_resolve_strerror(e) gai_strerror(e)

int tcp_listen(struct addrinfo *ai);

#endif
