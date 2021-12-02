#include <u.h>
#include <libc.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

int debug;

enum{
	Nvecinc = 32,
};

int
max(int a, int b)
{
	return a > b ? a : b;
}

int
min(int a, int b)
{
	return a < b ? a : b;
}

int
mobjfmt(Fmt *fmt)
{
	Mobj *mo;

	mo = va_arg(fmt->args, Mobj*);
	if(mo == nil)
		return fmtstrcpy(fmt, "[]");
	return fmtprint(fmt, "[%s:%#p:%P]", mo->o->name, mo, mo->Point);
}

void
dprint(char *fmt, ...)
{
	char s[256];
	va_list arg;

	if(!debug)
		return;
	va_start(arg, fmt);
	vseprint(s, s+sizeof s, fmt, arg);
	va_end(arg);
	fprint(2, "%s", s);
}

void
clearvec(Vector *v, int sz)
{
	if(v->p == nil)
		return;
	memset(v->p, 0, v->bufsz * sz);
	v->firstempty = 0;
	v->n = 0;
}

void *
pushsparsevec(Vector *v, void *e)
{
	int i;
	uchar *p, *q;

	i = v->firstempty;
	if(i == v->bufsz){
		v->p = erealloc(v->p, (v->bufsz + Nvecinc) * sizeof e,
			v->bufsz * sizeof e);
		v->bufsz += Nvecinc;
	}
	p = (uchar *)v->p + i * sizeof e;
	memcpy(p, e, sizeof e);
	for(i++, q=p+1; i<v->n; q++, i++)
		if(memcmp(p, nil, sizeof p) == 0)
			break;
	v->firstempty = i;
	v->n++;
	return p;
}

void *
pushvec(Vector *v, void *e, int sz)
{
	void *p;

	if(v->n == v->bufsz){
		v->p = erealloc(v->p, (v->bufsz + Nvecinc) * sz, v->bufsz * sz);
		v->bufsz += Nvecinc;
	}
	p = (uchar *)v->p + v->n * sz;
	memcpy(p, e, sz);
	v->n++;
	return p;
}

char *
estrdup(char *s)
{
	if((s = strdup(s)) == nil)
		sysfatal("estrdup: %r");
	setmalloctag(s, getcallerpc(&s));
	return s;
}

void *
erealloc(void *p, ulong n, ulong oldn)
{
	if((p = realloc(p, n)) == nil)
		sysfatal("realloc: %r");
	setrealloctag(p, getcallerpc(&p));
	if(n > oldn)
		memset((uchar *)p + oldn, 0, n - oldn);
	return p;
}

void *
emalloc(ulong n)
{
	void *p;

	if((p = mallocz(n, 1)) == nil)
		sysfatal("emalloc: %r");
	setmalloctag(p, getcallerpc(&n));
	return p;
}

vlong
flen(int fd)
{
	vlong l;
	Dir *d;

	if((d = dirfstat(fd)) == nil) 
		sysfatal("flen: %r");
	l = d->length;
	free(d);
	return l;
}
