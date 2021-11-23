#include <u.h>
#include <libc.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

Team teams[Nteam];
int nteam;

char *statename[OSend] = {
	[OSidle] "idle",
	[OSmove] "moving",
	[OSgather] "gathering",
};

static Mobjl mobjl0 = {.l = &mobjl0, .lp = &mobjl0}, *mobjl = &mobjl0;

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

void
refmobj(Mobj *mo)
{
	int n, i;
	Team *t;

	mo->mobjl = linkmobj(mobjl, mo, nil);
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

void
nextstate(Mobj *mo)
{
	Command *c;

	c = mo->cmds;
	if(c->cleanupfn != nil)
		c->cleanupfn(mo);
	if(c->nextfn != nil){
		c->initfn = c->nextfn;
		freezefrm(mo, mo->state);
		mo->state = OSskymaybe;	/* FIXME: kind of overloading this just for drw.c */
	}else
		popcommand(mo);
}

void
clearcommands(Mobj *mo)
{
	Command *c;

	c = mo->cmds;
	if(c->cleanupfn != nil)
		c->cleanupfn(mo);
	memset(mo->cmds, 0, sizeof mo->cmds);
	mo->ctail = 0;
	idlestate(mo);
}

void
abortcommands(Mobj *mo)
{
	dprint("%M abortcommand: %s\n", mo, mo->cmds[0].name);
	clearcommands(mo);
}

void
popcommand(Mobj *mo)
{
	dprint("%M popcommand: %s\n", mo, mo->cmds[0].name);
	if(--mo->ctail > 0){
		memmove(mo->cmds, mo->cmds+1, mo->ctail * sizeof *mo->cmds);
		mo->state = OSskymaybe;
	}else
		clearcommands(mo);
}

Command *
pushcommand(Mobj *mo)
{
	Command *c;

	dprint("%M pushcommand\n", mo);
	if(mo->ctail >= nelem(mo->cmds)){
		werrstr("command buffer overflow");
		return nil;
	}
	c = mo->cmds + mo->ctail++;
	if(mo->state == OSidle)
		mo->state = OSskymaybe;
	memset(c, 0, sizeof *c);
	return c;
}

static void
updatemobj(void)
{
	Mobjl *ml, *next;
	Mobj *mo;
	Command *c;

	for(ml=mobjl->l, next=ml->l; ml!=mobjl; ml=next, next=next->l){
		mo = ml->mo;
		if(mo->state == OSidle)
			continue;
		c = mo->cmds;
		if(mo->state == OSskymaybe && c->initfn(mo) < 0){
			abortcommands(mo);
			continue;
		}
		if(mo->state == OSskymaybe){
			dprint("%M updatemobj: %s cmd %s init bailed early\n",
				mo, mo->o->name, mo->cmds[0].name);
			nextstate(mo);
		}else
			c->stepfn(mo);
	}
}

void
stepsim(void)
{
	updatemobj();
}

void
initsim(void)
{
	int i;
	Team *t;

	if(nteam < 2)
		sysfatal("initgame: the only winning move is not to play");
	for(t=teams; t<=teams+nteam; t++)
		for(i=0; i<nelem(t->r); i++)
			t->r[i] = resources[i].init;
}
