#include <u.h>
#include <libc.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

Team teams[Nteam], *curteam;
int nteam;
int initres[Nresource], foodcap;

Mobjl *
linkmobj(Mobjl *l, Mobj *mo, Mobjl *p)
{
	if(p == nil)
		p = emalloc(sizeof *p);
	p->mo = mo;
	p->l = l->l;
	p->lp = l;
	l->l->lp = p;
	l->l = p;
	return p;
}

void
unlinkmobj(Mobjl *ml)
{
	if(ml == nil || ml->l == nil || ml->lp == nil)
		return;
	ml->lp->l = ml->l;
	ml->l->lp = ml->lp;
	ml->lp = ml->l = nil;
}

static void
refmobj(Mobj *mo)
{
	int n, i;
	Team *t;

	t = teams + mo->team;
	if(mo->o->f & (Fbuild|Fimmutable))
		t->nbuild++;
	else
		t->nunit++;
	n = t->firstempty;
	if(n == t->sz){
		t->mo = erealloc(t->mo, (t->sz + 32) * sizeof *t->mo, t->sz * sizeof *t->mo);
		t->sz += 32;
	}
	t->mo[n] = mo;
	mo->idx = mo->team << Teamshift | n;
	for(i=t->firstempty+1; i<t->sz; i++)
		if(t->mo[i] == nil)
			break;
	t->firstempty = i;
}

int
spawnunit(int x, int y, Obj *o, int team)
{
	Mobj *mo;

	if((mo = mapspawn(x, y, o)) == nil)
		return -1;
	mo->team = team;
	mo->θ = frand() * 256;
	mo->hp = o->hp;
	mo->state = OSidle;
	refmobj(mo);
	return 0;
}

int
spawnresource(int x, int y, Obj *o, int amount)
{
	int *t, *te;
	Mobj *mo;
	Resource *r;

	if(amount <= 0){
		werrstr("spawnresource: invalid amount");
		return -1;
	}
	if((mo = mapspawn(x, y, o)) == nil)
		return -1;
	mo->team = 0;
	mo->amount = amount;
	mo->state = OSrich;
	r = o->res;
	for(t=r->thresh, te=t+r->nthresh; t<te; t++){
		if(amount >= *t)
			break;
		mo->state++;
	}
	if(mo->state >= OSend){
		dprint("spawnresource %s %d,%d: invalid state %d\n", o->name, x, y, mo->state);
		mo->state = OSpoor;
	}
	refmobj(mo);
	return 0;
}

void
stepsim(void)
{
	updatemoves();
}

void
initsim(void)
{
	Team *t;

	if(nteam < 2)
		sysfatal("initgame: the only winning move is not to play");
	for(t=teams; t<=teams+nteam; t++)
		memcpy(t->r, initres, sizeof initres);
}
