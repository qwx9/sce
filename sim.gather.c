#include <u.h>
#include <libc.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

/* thanks: https://makingcomputerdothings.com/brood-war-api-the-comprehensive-guide-unit-movement-and-worker-behavios/ */
/* FIXME: additional bullshit logic */

enum{
	Twait = 8,
	Tgather = 75,	/* FIXME: 37 for gas, define in db? */
};

static void
cleanup(Mobj *)
{
}

static void
waitstep(Mobj *mo)
{
	Command *c;

	c = mo->cmds;
	if(--c->tc > 0)
		return;
	depleteresource(c->target1, Ngatheramount);
	pushreturncommand(mo, c->target1);
	nextstate(mo);
}

static void
gatherstep(Mobj *mo)
{
	Command *c;

	c = mo->cmds;
	if(++c->tc < Tgather)
		return;
	mo->state = OSwait;
	c->stepfn = waitstep;
	c->tc = nrand(Twait+1);
}

static int
pushgather(Mobj *mo)
{
	Command *c;

	c = mo->cmds;
	/* FIXME: check if resource still exists? (and amount >0) (needs despawning/death) */
	c->cleanupfn = cleanup;
	c->stepfn = gatherstep;
	c->nextfn = nil;
	c->goal = c->target1->Point;
	c->tc = 0;
	mo->state = OSgather;
	return 0;
}

int
pushgathercommand(Mobj *mo, Mobj *tgt)
{
	Command *c;

	if(tgt == nil){
		dprint("pushgathercommand: no target\n");
		return -1;
	}
	if((c = pushcommand(mo)) == nil){
		fprint(2, "pushmovecommand: %r\n");
		return -1;
	}
	c->name = "gather";
	c->initfn = pushmove;
	c->target1 = tgt;
	c->goal = tgt->Point;
	c->nextfn = pushgather;
	return 0;
}
