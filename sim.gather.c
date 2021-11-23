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
	Namount = 8,
};

static void
cleanup(Mobj *)
{
}

static void
returncargo(Mobj *mo)
{
	Resource *r;
	Command *c;

	c = mo->cmds;
	r = c->target1->o->res;
	assert(r != nil);
	teams[mo->team].r[r-resources] += Namount;
}

static void
waitstep(Mobj *mo)
{
	Command *c;

	c = mo->cmds;
	if(--c->tc > 0)
		return;
	nextstate(mo);
}

static void
step(Mobj *mo)
{
	Command *c;

	c = mo->cmds;
	if(++c->tc < Tgather)
		return;
	returncargo(mo);
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
	c->stepfn = step;
	c->nextfn = pushgather;
	c->tc = 0;
	mo->state = OSgather;
	return 0;
}

int
pushgathercommand(Point goal, Mobj *mo, Mobj *target)
{
	Command *c;

	if((c = pushcommand(mo)) == nil){
		fprint(2, "pushmovecommand: %r\n");
		return -1;
	}
	c->name = "gather";
	c->initfn = pushmove;
	c->goal = goal;
	c->target1 = target;
	c->nextfn = pushgather;
	return 0;
}
