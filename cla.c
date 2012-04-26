/* Central Legitimization Agency
 * - responds to requests from voters with a validation number
 * - track pair of (voter,validation number), don't give 1 voter
 *   more than 1 validation number
 *
 * Must listen for requests from voters. (TLS, password auth)
 * - for vnums.
 * - for who has voted and not voted.
 * Push validation numbers to CTF. (TLS, auth as client)
 * Request results data from CTF. (PULBIC)
 * Request vote/non-vote data from CTF. (TLS, auth as client)
 */
#define _GNU_SOURCE

#include "warn.h"
#include "tcp.h"
#include "proto.h"

#include "pthread_helper.h"
#include "accept_spawn.h"

#include <pthread.h>
#include <search.h>
#include <stdbool.h>

struct voter_rec {
	char *name;
	char *pass;
	valid_num_t vn;
	bool has_voted;
};

struct voters {
	void *root;
	unsigned ct;
};

struct pcc_arg {
	struct addrinfo *ctf_ai;
	struct voters *vs;
	pthread_t th;
};

static void *periodic_check_ctf(void *v_arg)
{
	/* TODO: periodically check the results from the CTF aren't lieing */
	struct pcc_arg *arg = v_arg;
	return NULL;
}

static void *periodic_voters_ctf(void *v_arg)
{
	/* TODO: update our knowledge of who voted */
	struct pcc_arg *arg = v_arg;
	return NULL;
}

static int voter_name_cmp(struct voter_rec *v1, struct voter_rec *v2)
{
	return strcmp(v1->name, v2->name);
}

static int voters_add_voter(struct voters *vs, struct voter_rec *vr)
{
	struct voter_rec *res = *(struct voter_rec **)tsearch(
			vr,
			vs->root,
			(comparison_fn_t)voter_name_cmp);

	if (res != vr) {
		/* already exsists */
		return 1;
	}

	/* new */
	return 0;
}

static int read_auth_line(struct voter_rec *vr, char *line, size_t line_len)
{
	char *pass = line;
	char *name   = strsep(&pass, "\t");
	if (pass == NULL) {
		return 1;
	}


	vr->name = name;
	vr->pass = pass;
	w_prt("name: %s pass: %s\n", name, pass);
	memset(&vr->vn, 0, sizeof(vr->vn));
	vr->has_voted = false;

	return 0;
}

static void voters_init(struct voters *vs)
{
	vs->root = NULL;
	vs->ct   = 0;
}

static int read_auth_file(struct voters *vs, char *fname)
{
	FILE *f = fopen(fname, "r");
	if (!f) {
		w_prt("error opening file %s\n", fname);
		return 1;
	}

	char *line = NULL;
	size_t sz  = 0;
	struct voter_rec *vr = malloc(sizeof(*vr));
	if (!vr) {
		w_prt("allocation failed.");
		fclose(f);
		return -ENOMEM;
	}

	for(;;) {
		ssize_t line_len = getline(&line, &sz, f);
		if (line_len == -1) {
			if (feof(f)) {
				fclose(f);
				free(line);
				return 0;
			} else {
				w_prt("error reading file: %d\n",
						ferror(f));
				fclose(f);
				free(line);
				free(vr);
				return 4;
			}
		}


		/* strip the newline */
		if (line[line_len - 1] == '\n')
			line[line_len - 1] = '\0';

		int r = read_auth_line(vr, line, line_len);
		if (r) {
			w_prt("malformed auth file line\n");
			continue;
		}

		r = voters_add_voter(vs, vr);
		if (r) {
			w_prt("voter is a duplicate, skipping.\n");
			continue;
		}

		vr = malloc(sizeof(*vr));
		if (!vr) {
			w_prt("allocation failed.");
			fclose(f);
			free(line);
			return -ENOMEM;
		}
	}
}

static int send_voters_to_ctf(struct addrinfo *ai, struct voters *vs)
{
	return -1;
}

static int cla_handle_packet(struct con_arg *arg, frame_op_t op,
		unsigned char *payload, size_t payload_len)
{
	switch(op) {
	default:
		return 1;
	}

	return 0;
}

int main(int argc, char *argv[])
{
	if (argc != 6) {
		w_prt(
		"usage: %s <listen addr> <listen port> <ctf addr> <ctf port> <auth file>\n",
			argc?argv[0]:"cla");
		return 1;
	}

	struct addrinfo *ctf_ai;
	int r = tcpw_resolve_as_client("ctf", argv[3], argv[4], &ctf_ai);
	if (r) {
		return 2;
	}

	struct voters vs;
	voters_init(&vs);

	r = read_auth_file(&vs, argv[5]);
	if (r) {
		w_prt("read auth file failed: %d\n", r);
		return 3;
	}

	r = send_voters_to_ctf(ctf_ai, &vs);
	if (r) {
		w_prt("send voters to ctf failed: %d\n", r);
		return 4;
	}

	pthread_attr_t th_attr;
	r = c_pthread_attr_init_detach(&th_attr);
	if (r) {
		return 5;
	}

	struct pcc_arg pa = {
		.ctf_ai = ctf_ai,
		.vs     = &vs
	};

	r = pthread_create(&pa.th, &th_attr, periodic_check_ctf, &pa);
	if (r) {
		w_prt("could not start periodic ctf check thread: %s\n",
				strerror(r));
		return 5;
	}

	r = pthread_create(&pa.th, &th_attr, periodic_voters_ctf, &pa);
	if (r) {
		w_prt("could not start periodic voters thread: %s\n",
				strerror(r));
		return 6;
	}


	int tl = tcpw_listen(argv[1], argv[2]);
	if (tl == -1) {
		return 7;
	}

	return accept_spawn_loop(tl, cla_handle_packet, NULL);
}
