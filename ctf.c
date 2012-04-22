/* Central Tabulating Facility
 * - maintain a list of validation numbers from CLA (CLA pushes these over).
 * - recieves a (vote,random id number,validation number) from voter
 *   - crosses off validation num.
 *   - adds random id num to vote
 *
 * Listen to vote data from voters. (TLS, no client validation)
 * Listen for requests for election results from all. (PUBLIC)
 * Listen for requests from CLA for vote/non-vote data. (TLS, client auth)
 */

#include "warn.h"
#include "tcp.h"
#include "proto.h"

#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <pthread.h>

struct con_arg {
	int c_id;
	int cfd;
	struct sockaddr_storage address;
	socklen_t address_len;
	pthread_t th;
};

pthread_mutex_t con_prt_mut = PTHREAD_MUTEX_INITIALIZER;

void con_prt(struct con_arg const *arg, char const *fmt, ...)
{
#if 0
	char host[128];
	char serv[32];

	int r = getnameinfo((struct sockaddr *)&arg->address,
			arg->address_len,
			host, sizeof(host), serv, sizeof(serv),
			NI_NUMERICHOST, NI_NUMERICSERV);
#endif

	pthread_mutex_lock(&con_prt_mut);

	fprintf(stderr, "con %03d: ", arg->c_id);

	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	pthread_mutex_unlock(&con_prt_mut);
}

void *con_th(void *v_arg)
{
	struct con_arg *arg = v_arg;
	int cfd = arg->cfd;
	unsigned char buf[128];
	unsigned buf_occ = 0;

	int spawn_time = time(NULL);
	int state      = S_NEW;

	for (;;) {
		ssize_t r = recv(cfd, buf + buf_occ, sizeof(buf) - buf_occ, 0);
		if (r == -1) {
			con_prt(arg, "recv failed\n");
			/* FIXME: bailout as needed */
			continue;
		}

		buf_occ += r;

		if (buf_occ < FRAME_LEN_BYTES + FRAME_OP_BYTES) {
			/* minimal frame has only LEN & OP */
			continue;
		}

		frame_len_t len = decode_len(buf);
		if (len > sizeof(buf) - FRAME_LEN_BYTES || len < FRAME_OP_BYTES) {
			con_prt(arg, "frame has bad len: %u\n", len);
			/* FIXME: close and die, do
			 * not attempt to recover from possible
			 * desync. */
			continue;
		}

		if (buf_occ < (len + FRAME_LEN_BYTES)) {
			/* not enough data to complete frame */
			continue;
		}

		/* we have a frame */
		frame_op_t op = decode_op(buf);

		switch(op) {
		case OP_VOTE:
			vote_parse(buf, len);
			break;
		case OP_REQ_RESULTS:
			break;
		case OP_REQ_VOTERS:
			break;
		case OP_STARTTLS:
			break;
		}
	}
}

int main(int argc, char *argv[])
{
	if (argc != 3) {
		w_prt("usage: %s <listen addr> <listen port>\n",
				argc?argv[0]:"ctf");
		return 1;
	}

	struct addrinfo *res;
	int r = tcp_resolve_listen(argv[1], argv[2], &res);

	if (r) {
		/* error resolving. */
		w_prt("listen addr resolve error: %s\n", tcp_resolve_strerror(r));
		return 2;
	}

	int tl = tcp_listen(res);
	freeaddrinfo(res);
	if (tl == -1) {
		w_prt("could create listener: %s\n", strerror(errno));
		return 3;
	}

	r = listen(tl, 128);
	if (r == -1) {
		w_prt("failed to start listening: %s\n", strerror(errno));
		return 4;
	}

	pthread_attr_t th_attr;
	r = pthread_attr_init(&th_attr);
	if (r) {
		w_prt("pthread_attr_init: %s\n", strerror(r));
		return 5;
	}

	r = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	if (r) {
		w_prt("pthread_attr_setdetachstate: %s\n", strerror(r));
	}

	int c_id = 0;
	struct con_arg *ca = malloc(sizeof(*ca));
	ca->c_id = c_id;

	for(;;) {
		int cfd = accept(tl, &ca->address, &ca->address_len);
		if (cfd == -1) {
			w_prt("accept failed: %s\n", strerror(errno));
			switch(errno) {
			case ECONNABORTED:
			case EINTR:
				/* definitely retry */
				continue;
			case EMFILE:
			case ENFILE:
			case ENOMEM:
			case ENOBUFS:
				/* indicate overloaded system */
				continue;

			case EAGAIN:
				/* should never occur */

			case EBADF:
			case EINVAL:
			case ENOTSOCK:
			case EOPNOTSUPP:
			case EPROTO:
			default:
				/* actually (probably) fatal */
			}

			return -1;
		}

		ca->cfd = cfd;
		r = pthread_create(&ca->th, &attr, con_th, ca);
		if (r) {
			w_prt("pthread_create: %s\n", strerror(r));
			continue;
		}

		/* TODO: track created threads? */
		c_id ++;
		ca = malloc(sizeof(*ca));
		ca->c_id = c_cid;
	}

	return 0;
}
