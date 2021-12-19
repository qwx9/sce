#include <u.h>
#include <libc.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

enum{
	Twait = 8,
};

static void
cleanup(Mobj *)
{
}

static void
waitstep(Mobj *mo)
{
	Resource *r;
	Command *c;

	c = mo->cmds;
	if(--c->tc > 0)
		return;
	r = c->target2->o->res;
	assert(r != nil);
	teams[mo->team].r[r-resources] += Ngatheramount;
	pushgathercommand(mo, c->target2);
	nextstate(mo);
}

static int
pushdrop(Mobj *mo)
{
	Command *c;

	c = mo->cmds;
	c->cleanupfn = cleanup;
	c->nextfn = nil;
	c->stepfn = waitstep;
	c->tc = nrand(Twait+1);
	mo->state = OSwait;
	return 0;
}

static Mobj *
finddrop(Mobj *mo)
{
	double d, d´;
	Team *t;
	Mobj *wo, *w, **wp, **we;

	t = teams + mo->team;
	if(t->drops.n < 1){
		werrstr("no drops");
		return nil;
	}
	d = mapwidth * mapheight;
	for(wp=t->drops.p, we=wp+t->drops.n, wo=nil; wp<we; wp++){
		w = *wp;
		d´ = octdist(mo->Point, w->Point);
		if(d´ < d){
			wo = w;
			d = d´;
		}
	}
	return wo;
}

int
pushreturncommand(Mobj *mo, Mobj *ro)
{
	Command *c;

	if((c = pushcommand(mo)) == nil){
		fprint(2, "pushreturncommand: %r\n");
		return -1;
	}
	if((c->target1 = finddrop(mo)) == nil)
		return -1;
	c->name = "return";
	c->initfn = pushmove;
	c->goal = c->target1->Point;
	c->target2 = ro;
	c->nextfn = pushdrop;
	return 0;
}
