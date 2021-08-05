#include <u.h>
#include <libc.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

Team teams[Nteam], *curteam;
int nteam;
int initres[Nresource], foodcap;

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
nextaction(Mobj *mo)
{
	assert(mo->actp != nil);
	if(mo->actp->cleanupfn != nil)
		mo->actp->cleanupfn(mo);
	mo->actp++;
	if((mo->state = mo->actp->os) == OSskymaybe){
		dprint("A nextaction %s %#p: done\n", mo->o->name, mo);
		mo->actp = nil;
		popcommand(mo);
		return;
	}
	dprint("A nextaction %s %#p: %s\n", mo->o->name, mo, mo->actp->name);
}

int
pushactions(Mobj *mo, Action *a)
{
	mo->actp = a;
	mo->state = a->os;
	dprint("A pushaction %s %#p: %s\n", mo->o->name, mo, a->name);
	return 0;
}

void
clearcommands(Mobj *mo)
{
	dprint("C clearcommand %s %#p: %s\n", mo->o->name, mo, mo->cmds[0].name);
	if(mo->actp != nil && mo->actp->cleanupfn != nil)
		mo->actp->cleanupfn(mo);
	mo->actp = nil;
	memset(mo->cmds, 0, sizeof mo->cmds);
	mo->ctail = 0;
	idlestate(mo);
}

void
abortcommands(Mobj *mo)
{
	dprint("C abortcommand %s %#p: %s\n", mo->o->name, mo, mo->cmds[0].name);
	clearcommands(mo);
}

void
popcommand(Mobj *mo)
{
	dprint("C popcommand %s %#p: %s\n", mo->o->name, mo, mo->cmds[0].name);
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

	dprint("C pushcommand %s %#p\n", mo->o->name, mo);
	if(mo->ctail >= nelem(mo->cmds)){
		werrstr("command buffer overflow");
		return nil;
	}
	c = mo->cmds + mo->ctail++;
	if(mo->state == OSidle)
		mo->state = OSskymaybe;
	return c;
}

static void
updatemobj(void)
{
	Mobjl *ml, *next;
	Mobj *mo;

	for(ml=mobjl->l, next=ml->l; ml!=mobjl; ml=next, next=next->l){
		mo = ml->mo;
		if(mo->state == OSidle)
			continue;
		if(mo->actp == nil
		&& (mo->cmds[0].initfn(mo) < 0 || mo->actp == nil || mo->state == OSskymaybe)){
			abortcommands(mo);
			continue;
		}
		if(mo->state == OSskymaybe)
			sysfatal("updatemobj: %s cmd %s impossible/stale state %d",
				mo->o->name, mo->cmds[0].name, mo->state);
		mo->actp->stepfn(mo);
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
	Team *t;

	if(nteam < 2)
		sysfatal("initgame: the only winning move is not to play");
	for(t=teams; t<=teams+nteam; t++)
		memcpy(t->r, initres, sizeof initres);
}
