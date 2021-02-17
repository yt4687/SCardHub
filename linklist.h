#pragma once

struct linknode {
	struct linknode *next;
	struct linknode *prev;
};

static __inline void node_init(void *list) {
	struct linknode *p = list;
	p->next = p;
	p->prev = p;
}

static __inline void *node_cut(void *node) {
	struct linknode *p = node;
	p->next->prev = p->prev;
	p->prev->next = p->next;
	return p;
}

static __inline int is_list_empty(void *list) {
	struct linknode *p = list;
	return p == p->next;
}

static __inline void list_move(void *dst, void *src) {
	struct linknode *s = src;
	struct linknode *t;
	struct linknode *q = dst;
	struct linknode *r;

	if (s == s->next)
		return; /* src is empty */

	s = s->next;

	node_cut(src);
	node_init(src);

	t = s->prev; /* (s::t) */
	r = q->prev; /* (q::r) */

	/* Insert src to tail(=prev) of dst. */

	/* (q::r) (s::t) */

	r->next = s;
	s->prev = r;
	t->next = q;
	q->prev = t;
}

#if 0
static __inline void list_move_to_head(void *dst, void *src) {
	struct linknode *t = dst;
	list_move(t->next, src);
}

static __inline void list_move_to_tail(void *dst, void *src) {
	list_move(dst, src);
}
#endif

static __inline void list_push(void *list, void *node) {
	struct linknode *s = list;
	struct linknode *t = s->prev;
	struct linknode *q = node;
	/* t q s */
	q->next = s;
	q->prev = t;
	t->next = q;
	s->prev = q;
}

static __inline void *list_pop(void *list) {
	struct linknode *l = list;
	if (l->prev == l)
		return 0;
	return node_cut(l->prev);
}

static __inline void *list_shift(void *list) {
	struct linknode *l = list;
	if (l->next == l)
		return 0;
	return node_cut(l->next);
}

static __inline void list_unshift(void *list, void *node) {
	struct linknode *s = list;
	struct linknode *t = s->next;
	struct linknode *q = node;
	/* s q t */
	q->next = t;
	q->prev = s;
	s->next = q;
	t->prev = q;
}

static __inline int list_count(const void *list) {
	const struct linknode *l = list;
	const struct linknode *p = l->next;
	int n = 0;
	while (p != l) {
		n++;
		p = p->next;
	}
	return n;
}
