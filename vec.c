#include <u.h>
#include <libc.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

enum{
	Nvecinc = 32,
};

void
clearvec(Vector *v)
{
	if(v->p == nil)
		return;
	memset(v->p, 0, v->totsz);
	v->firstempty = 0;
	v->n = 0;
}

static void *
growvec(Vector *v, int n)
{
	if(n < v->bufsz)
		return (uchar *)v->p + n * v->elsz;
	v->p = erealloc(v->p, v->totsz + Nvecinc * v->elsz, v->totsz);
	v->bufsz += Nvecinc;
	v->totsz += Nvecinc * v->elsz;
	return (uchar *)v->p + n * v->elsz;
}

void
popsparsevec(Vector *v, int n)
{
	assert(v != nil && v->elsz > 0 && n >= 0 && n < v->n);
	memset((uchar *)v->p + n * v->elsz, 0, v->elsz);
	if(n < v->firstempty)
		v->firstempty = n;
}

/* assumes that zeroed element means empty; could fill with
 * magic values instead */
void *
pushsparsevec(Vector *v, void *e)
{
	int n;
	uchar *p, *q;

	assert(v != nil && v->elsz > 0);
	n = v->firstempty;
	p = growvec(v, n);
	for(n++, q=p+v->elsz; n<v->n; n++, q+=v->elsz)
		if(memcmp(p, q, v->elsz) == 0)
			break;
	v->firstempty = n;
	memcpy(p, e, v->elsz);
	v->n++;
	return p;
}

void *
pushvec(Vector *v, void *e)
{
	uchar *p;

	assert(v != nil && v->elsz > 0);
	p = growvec(v, v->n);
	memcpy(p, e, v->elsz);
	v->n++;
	v->firstempty = v->n;
	return p;
}

void *
newvec(Vector *v, int nel, int elsz)
{
	assert(v != nil && elsz > 0);
	v->elsz = elsz;
	nel = nel + Nvecinc-1 & ~(Nvecinc-1);
	v->bufsz = nel;
	v->totsz = nel * elsz;
	v->p = emalloc(v->totsz);
	return v->p;
}
