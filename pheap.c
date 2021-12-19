#include <u.h>
#include <libc.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

static Pairheap *
mergequeue(Pairheap *a, Pairheap *b)
{
	if(b == nil)
		return a;
	else if(a->sum < b->sum){
		b->right = a->left;
		a->left = b;
		b->parent = a;
		return a;
	}else{
		a->right = b->left;
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
	b->parent = nil;
	c = b->right;
	b->right = nil;
	return mergequeue(mergequeue(a, b), mergepairs(c));
}

void
nukequeue(Pairheap **queue)
{
	Pairheap *p;

	while((p = popqueue(queue)) != nil)
		free(p);
}

Pairheap *
popqueue(Pairheap **queue)
{
	Pairheap *p;

	p = *queue;
	if(p == nil)
		return nil;
	*queue = mergepairs(p->left);
	dprint("pop %#p %P g %f sum %f\n", p->n, p->n->Point, p->n->g, p->sum);
	return p;
}

void
decreasekey(Pairheap *p, double Δ, Pairheap **queue)
{
	p->sum -= Δ;
	p->n->g -= Δ;
	dprint("decrease %#p %P g %f sum %f\n", p->n, p->n->Point, p->n->g, p->sum);
	if(p->parent != nil && p->sum < p->parent->sum){
		p->parent->left = nil;
		p->parent = nil;
		*queue = mergequeue(p, *queue);
	}
}

void
pushqueue(Node *n, Pairheap **queue)
{
	Pairheap *p;

	p = emalloc(sizeof *p);
	p->n = n;
	p->sum = n->h + n->g;
	n->p = p;
	dprint("push %#p %P g %f sum %f\n", p->n, p->n->Point, p->n->g, p->sum);
	*queue = mergequeue(p, *queue);
}
