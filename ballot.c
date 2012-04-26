#include "ballot.h"


struct ballot_option *ballot_option_create(size_t len)
{
	struct ballot_option *opt = malloc(offsetof(typeof(*opt), data[bo_len]));
	if (!opt) {
		return NULL;
	}

	opt->len = bo_len;
	opt->ref = 1;

	return opt;
}

void ident_num_init(ident_num_t *in)
{
	unsigned i;
	for (i = 0; i < IDENT_NUM_BYTES; i++) {
		in->data[i] = rand();
	}
}

