#include <u.h>
#include <libc.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

Mobj *
derefmobj(int idx, long uuid)
{
	int n;
	Mobj *mo;
	Team *t;

	n = idx >> Teamshift & Nteam - 1;
	if(n < 0 || n > nteam){
		werrstr("invalid team number %d", n);
		return nil;
	}
	t = teams + n;
	n = idx & Teamidxmask;
	if(n > t->sz || (mo = t->mo[n]) == nil){
		werrstr("mobj index %d out of bounds or missing", n);
		return nil;
	}
	if(mo->idx != idx || mo->uuid != uuid){
		werrstr("phase error: %#ux,%ld ≠ %M %#ux,%ld",
			idx, uuid, mo, mo->idx, mo->uuid);
		return nil;
	}
	return mo;
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
	idlestate(mo);
	refmobj(mo);
	return 0;
}

int
spawnresource(int x, int y, Obj *o, int amount)
{
	Mobj *mo;

	if(amount <= 0){
		werrstr("spawnresource: invalid amount");
		return -1;
	}
	if((mo = mapspawn(x, y, o)) == nil)
		return -1;
	mo->team = 0;
	mo->amount = amount;
	resourcestate(mo);
	refmobj(mo);
	return 0;
}
