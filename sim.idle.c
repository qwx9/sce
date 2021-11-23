#include <u.h>
#include <libc.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

void
freezefrm(Mobj *mo, int oldstate)
{
	Pics *old, *new;

	old = mo->o->pics[oldstate];
	new = mo->o->pics[OSidle];
	if(!new->freeze || !old->shared){
		mo->freezefrm = 0;
		return;
	}
	mo->freezefrm = tc % old[PTbase].nf;
	if(mo->freezefrm > new[PTbase].nf)
		sysfatal("idle:freezefrm obj %s: invalid frame number %d > %d",
			mo->o->name, mo->freezefrm, new[PTbase].nf);
}

void
idlestate(Mobj *mo)
{
	freezefrm(mo, mo->state);
	mo->state = OSidle;
}
