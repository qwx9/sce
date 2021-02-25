#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

extern QLock drawlock;

vlong tc;

static int tdiv;

static void
step(vlong tics)
{
	qlock(&drawlock);
	while(!pause && tics-- > 0)
		stepsim();
	qunlock(&drawlock);
}

static void
simproc(void *sys)
{
	vlong t, t0, dt, Δtc;

	USED(sys);
	initsim();
	Δtc = 1;
	t0 = nsec();
	for(;;){
		step(Δtc);
		tc += 1;
		t = nsec();
		Δtc = (t - t0) / tdiv;
		if(Δtc <= 0)
			Δtc = 1;
		t0 += Δtc * tdiv;
		dt = (t0 - t) / Te6;
		if(dt > 0)
			sleep(dt);
	}
}

void
initsv(int tv, char *sys)
{
	tdiv = Te9 / (tv * 3);
	if(proccreate(simproc, sys, 8192) < 0)
		sysfatal("proccreate: %r");
}
