#include <u.h>
#include <libc.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

/* thanks: https://makingcomputerdothings.com/brood-war-api-the-comprehensive-guide-unit-movement-and-worker-behavios/ */
/* FIXME: additional bullshit logic */

enum{
	Tgather = 75,	/* FIXME: 37 for gas, define in db? */
	Namount = 8,
};

static void
cleanup(Mobj *)
{
}

static void
step(Mobj *mo)
{
	Command *c;

	c = mo->cmds;
	if(++c->tc >= Tgather){
		nextstate(mo);
		return;
	}
	// FIXME: butts
}

static int
pushgather(Mobj *mo)
{
	Command *c;

	c = mo->cmds;
	/* FIXME: check if resource still exists? (and amount >0) (needs despawning/death) */
	c->cleanupfn = cleanup;
	c->stepfn = step;
	c->nextfn = nil;
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
