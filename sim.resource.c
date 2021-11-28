#include <u.h>
#include <libc.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

void
resourcestate(Mobj *mo)
{
	int os, *t, *te;
	Resource *r;

	r = mo->o->res;
	for(os=OSrich, t=r->thresh, te=t+r->nthresh; t<te; t++, os++)
		if(mo->amount >= *t)
			break;
	mo->state = os;
}

void
depleteresource(Mobj *mo, int n)
{
	/* FIXME: despawn when getting to 0 */
	mo->amount -= n;
	resourcestate(mo);
}
