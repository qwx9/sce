#include <u.h>
#include <libc.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

/* Fredman, M.L., Sedgewick, R., Sleator, D.D.  et al.  The pairing
 * heap: A new form of self-adjusting heap.  Algorithmica 1, 111–129
 * (1986).  */

/* this implementation requires a bigger stack size if the heap can
 * grow big (8192 is already insufficient with 40-50 nodes);
 * otherwise, stack overflows hidden behind more cryptic memory pool
 * corruption errors will occur */

static void
checkheap(Pairheap *p, Pairheap *queue)
{
	Pairheap *q;

	if(p == nil || queue == nil)
		return;
	if(p == queue)
		fprint(2, "pheap::checkheap %#p\n", p);
	assert(p == queue || p->parent != nil && p->parent != p);
	assert(p->parent == nil || p->parent->right == p || p->parent->left == p);
	if(p->parent != nil && p->parent->left == p)
		assert(p->parent->sum <= p->sum);
	for(q=p; q!=nil; q=q->right)
		checkheap(q->left, queue);
}

static void
printright(Pairheap *p, int level)
{
	int i;
	Pairheap *q;

	if(p == nil)
		return;
	fprint(2, "(\n");
	for(i=0; i<level; i++)
		fprint(2, "\t");
	for(q=p; q!=nil; q=q->right){
		fprint(2, "[%#p %.5f] — ", q, q->sum);
		printright(q->left, level+1);
	}
	fprint(2, "\n");
	for(i=0; i<level-1; i++)
		fprint(2, "\t");
	fprint(2, ")");
}
void
printqueue(Pairheap **queue)
{
	if(queue == nil)
		return;
	fprint(2, "pheap::printqueue %#p ", *queue);
	printright(*queue, 1);
	fprint(2, "\n");
}

static void
debugqueue(Pairheap **queue)
{
	if(!debug || queue == nil)
		return;
	printqueue(queue);
	checkheap(*queue, *queue);
}

static Pairheap *
mergequeue(Pairheap *a, Pairheap *b)
{
	dprint("pheap::mergequeue %#p %.6f b %#p %.6f\n",
		a, a->sum, b, b!=nil ? b->sum : NaN());
	if(b == nil)
		return a;
	else if(a->sum < b->sum){
		b->right = a->left;
		if(a->left != nil)
			a->left->parent = b;
		a->left = b;
		b->parent = a;
		return a;
	}else{
		a->right = b->left;
		if(b->left != nil)
			b->left->parent = a;
		b->left = a;
		a->parent = b;
		return b;
	}
}

static Pairheap *
mergepairs(Pairheap *a)
{
	Pairheap *b, *c;

	if(a == nil)
		return nil;
	a->parent = nil;
	b = a->right;
	if(b == nil)
		return a;
	a->right = nil;
	c = b->right;
	b->parent = nil;
	b->right = nil;
	return mergequeue(mergequeue(a, b), mergepairs(c));
}

void
nukequeue(Pairheap **queue)
{
	Pairheap *p;

	dprint("pheap::nukequeue %#p\n", queue);
	while((p = popqueue(queue)) != nil){
		debugqueue(&p);
		free(p);
	}
}

Pairheap *
popqueue(Pairheap **queue)
{
	Pairheap *p;

	p = *queue;
	if(p == nil)
		return nil;
	dprint("pheap::popqueue %#p %.6f\n", p, p->sum);
	*queue = mergepairs(p->left);
	debugqueue(queue);
	p->left = p->right = nil;
	return p;
}

void
decreasekey(Pairheap *p, double Δ, Pairheap **queue)
{
	dprint("pheap::decreasekey %#p %.6f Δ %.6f\n", p, p->sum, Δ);
	if(p->parent == nil){
		p->sum -= Δ;
		return;
	}
	if(p->parent->left == p){
		assert(p->parent->sum <= p->sum);
		p->parent->left = p->right;
	}else{
		assert(p->parent->right == p);
		p->parent->right = p->right;
	}
	if(p->right != nil)
		p->right->parent = p->parent;
	p->right = nil;
	p->parent = nil;
	p->sum -= Δ;
	*queue = mergequeue(p, *queue);
	debugqueue(queue);
}

Pairheap *
pushqueue(Node *n, Pairheap **queue)
{
	Pairheap *p;

	p = emalloc(sizeof *p);
	p->sum = n->h + n->g;
	p->n = n;
	n->p = p;
	dprint("pheap::pushqueue %#p %.6f\n", p, p->sum);
	*queue = mergequeue(p, *queue);
	debugqueue(queue);
	return p;
}
