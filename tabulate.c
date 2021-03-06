
#define _GNU_SOURCE

#include "warn.h"
#include "tabulate.h"
#include "ballot.h"

#include <search.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>

static void ident_num_rec_init(struct ident_num_rec *inr, struct vote *v)
{
	list_init(&inr->l);
	memcpy(&inr->id, &v->id, sizeof(inr->id));
}

static void vote_rec_init(struct vote_rec *vr, struct ballot_option *ba)
{
	vr->opt = bo_ref_inc(ba);
	vr->vote_count = 0;
	list_init(&vr->ident_nums);
	list_init(&vr->l);
}

static void valid_num_rec_init(
		struct valid_num_rec *vnr,
		valid_num_t *vn)
{
	memcpy(&vnr->vn, vn, sizeof(*vn));
	list_init(&vnr->l);
}

static int valid_num_cmp(struct valid_num_rec *r1, struct valid_num_rec *r2)
{
	return memcmp(&r1->vn, &r2->vn, sizeof(valid_num_t));
}

static int vns_insert(struct valid_num_store *vns, valid_num_t *vn)
{
	struct valid_num_rec *vnr = malloc(sizeof(*vnr));
	if (!vnr) {
		w_prt("no mem.\n");
		return -ENOMEM;
	}

	valid_num_rec_init(vnr, vn);

	struct valid_num_rec *res = *(struct valid_num_rec **)tsearch(
			vnr,
			&vns->root,
			(comparison_fn_t)valid_num_cmp);

	if (res != vnr) {
		/* already exsists */
		w_prt("vnum already exsists\n");
		free(vnr);
		return TABU_ALREADY_EXISTS;
	}

	/* new */
	return 0;
}

static struct valid_num_rec *vns_find_vn(
		struct valid_num_store *vns,
		valid_num_t *vn)
{
	struct valid_num_rec vnr;
	valid_num_rec_init(&vnr, vn);

	struct valid_num_rec **res = (struct valid_num_rec **)tfind(&vnr,
			&vns->root,
			(comparison_fn_t)valid_num_cmp);

	if (!res) {
		/* is new... */
		return NULL;
	}

	/* is old */
	return *res;
}

bool tabu_has_results(tabu_t *tab)
{
	pthread_mutex_lock(&tab->mut);
	bool r = tab->vs.vote_recs > 0;
	pthread_mutex_unlock(&tab->mut);
	return r;
}

int tabu_add_valid_num(tabu_t *tab, valid_num_t *vn)
{
	pthread_mutex_lock(&tab->mut);
	int r = vns_insert(&tab->vns, vn);
	pthread_mutex_unlock(&tab->mut);
	return r;
}

static int vote_rec_cmp(struct vote_rec *v1, struct vote_rec *v2)
{
	struct ballot_option *b1 = v1->opt, *b2 = v2->opt;
	if (b1->len > b2->len)
		return -1;
	else if (b1->len < b2->len)
		return 1;
	else
		return memcmp(b1->data, b2->data, b1->len);
}

static int vs_add_vote(struct vote_store *vs, struct vote *v)
{
	struct vote_rec *vr = malloc(sizeof(*vr));
	if (!vr) {
		return -ENOMEM;
	}

	vote_rec_init(vr, v->opt);

	struct vote_rec **res = (struct vote_rec **)tsearch(
			vr,
			&vs->root,
			(comparison_fn_t)vote_rec_cmp);
	if (!res)
		return -ENOMEM;

	if (*res != vr) {
		free(vr);
	} else {
		/* number of `vote_rec`s increased */
		vs->vote_recs++;
		list_add(&vs->vr_list, &(*res)->l);
	}

	vs->votes ++;

	struct ident_num_rec *ir = malloc(sizeof(*ir));
	if (!ir) {
		return -ENOMEM;
	}

	ident_num_rec_init(ir, v);

	list_add(&(*res)->ident_nums, &ir->l);

	(*res)->vote_count ++;

	return 0;
}

unsigned tabu_vote_ct(tabu_t *tab)
{
	pthread_mutex_lock(&tab->mut);
	unsigned r = tab->vs.votes;
	pthread_mutex_unlock(&tab->mut);
	return r;
}

static int vote_store_init(struct vote_store *vs)
{
	vs->vote_recs = 0;
	vs->votes     = 0;
	vs->root      = NULL;

	list_init(&vs->vr_list);
	return 0;
}

static int valid_num_store_init(struct valid_num_store *vns)
{
	vns->ct = 0;
	vns->root = NULL;
	list_init(&vns->used);
	return 0;
}

int tabu_init(tabu_t *t)
{
	int r = valid_num_store_init(&t->vns);
	if (r)
		return r;

	r = vote_store_init(&t->vs);
	if (r)
		return r;

	return pthread_mutex_init(&t->mut, NULL);
}

int tabu_insert_vote(tabu_t *t, struct vote *v)
{
	pthread_mutex_lock(&t->mut);

	/* check if validation number is valid and unused */
	struct valid_num_rec *vnr = vns_find_vn(&t->vns, &v->vn);
	if (!vnr) {
		/* no such validation number. */
		pthread_mutex_unlock(&t->mut);
		return TABU_BAD_VALIDATION;
	}

	if (!list_is_empty(&vnr->l)) {
		pthread_mutex_unlock(&t->mut);
		return TABU_ALREADY_VOTED;
	}


	/* add vote */
	int r = vs_add_vote(&t->vs, v);

	if (!r) {
		/* mark the vnum as used */
		list_add(&t->vns.used, &vnr->l);
	}

	pthread_mutex_unlock(&t->mut);
	return r;
}

int tabu_for_each_vote_rec(tabu_t *tab, vote_rec_cb cb, void *pdata)
{
	pthread_mutex_lock(&tab->mut);
	struct vote_rec *vr;
	list_for_each_entry(vr, &tab->vs.vr_list, l) {
		int r = cb(vr, pdata);
		if (r) {
			pthread_mutex_unlock(&tab->mut);
			return r;
		}
	}
	pthread_mutex_unlock(&tab->mut);
	return 0;
}

int tabu_for_each_voted_valid_num_rec(tabu_t *tab, valid_num_rec_cb cb, void *pdata)
{
	pthread_mutex_lock(&tab->mut);
	struct valid_num_rec *vnr;
	list_for_each_entry(vnr, &tab->vns.used, l) {
		int r = cb(vnr, pdata);
		if (r) {
			pthread_mutex_unlock(&tab->mut);
			return r;
		}
	}
	pthread_mutex_unlock(&tab->mut);
	return 0;
}
