#include <u.h>
#include <libc.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

enum{
	Ninc = 128,
};

/* no nodes are ever freed */
static QNode freeq = { .next = &freeq, .prev = &freeq };

static void
qtlink(QNode *q)
{
	assert(q != nil);
	q->prev = freeq.prev;
	q->next = &freeq;
	freeq.prev->next = q;
	freeq.prev = q;
}

static void
qtfree(QNode *q)
{
	if(q == nil)
		return;
	free(q->aux);
	memset(q, 0, sizeof *q);
	qtlink(q);
}

static QNode *
qtalloc(void)
{
	QNode *q, *fq;

	if(freeq.next == &freeq){
		q = emalloc(Ninc * sizeof *q);
		for(fq=q; fq<q+Ninc; fq++)
			qtlink(fq);
	}
	q = freeq.next;
	q->next->prev = &freeq;
	freeq.next = q->next;
	q->prev = q->next = nil;
	return q;
}

QNode *
qtmerge(QNode *q)
{
	if(q == nil)
		return nil;
	qtmerge(q->left);
	qtmerge(q->right);
	qtmerge(q->up);
	qtmerge(q->down);
	qtfree(q->left);
	qtfree(q->right);
	qtfree(q->up);
	qtfree(q->down);
	q->left = q->right = q->up = q->down = nil;
	dprint("qtmerge %#p\n", q);
	return q;
}

QNode *
qtsplit(QNode *q)
{
	assert(q->left == nil && q->right == nil && q->up == nil && q->down == nil);
	dprint("qtsplit %#p\n", q);
	q->left = qtalloc();
	q->right = qtalloc();
	q->up = qtalloc();
	q->down = qtalloc();
	return q;
}
