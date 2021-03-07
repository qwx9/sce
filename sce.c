#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <mouse.h>
#include <keyboard.h>
#include <pool.h>
#include "dat.h"
#include "fns.h"

enum{
	Hz = 60,
};

char *progname = "sce", *dbname, *prefix, *mapname = "map1.db";
int debugmap;
QLock drawlock;

typedef struct Kev Kev;
typedef struct Mev Mev;
struct Kev{
	int down;
	Rune r;
};
struct Mev{
	Point;
	Point Δ;
	int b;
};

static Channel *reszc, *kc, *mc, *tmc;

static void
mproc(void *)
{
	int n, fd, nerr;
	char buf[1+5*12];
	Mev m, om;

	if((fd = open("/dev/mouse", OREAD)) < 0)
		sysfatal("mproc: %r");
	nerr = 0;
	memset(&om, 0, sizeof om);
	for(;;){
		if((n = read(fd, buf, sizeof buf)) != 1+4*12){
			if(n < 0 || ++nerr > 10)
				break;
			fprint(2, "mproc: bad count %d not 49: %r\n", n);
			continue;
		}
		nerr = 0;
		switch(buf[0]){
		case 'r': send(reszc, nil); /* wet floor */
		case 'm':
			m.x = strtol(buf+1+12*0, nil, 10);
			m.y = strtol(buf+1+12*1, nil, 10);
			m.b = strtol(buf+1+12*2, nil, 10);
			m.Δ.x = m.x - om.x;
			m.Δ.y = m.y - om.y;
			if((m.b & 1) == 1 && (om.b & 1) == 0
			|| (m.b & 4) == 4 && (om.b & 4) == 0
			|| m.b & 2)
				send(mc, &m);
			om = m;
			break;
		}
	}
}

static void
kproc(void *)
{
	int n, fd;
	char buf[256], down[128], *s, *p;
	Rune r;
	Kev ke;

	if((fd = open("/dev/kbd", OREAD)) < 0)
		sysfatal("kproc: %r");
	memset(buf, 0, sizeof buf);
	memset(down, 0, sizeof down);
	for(;;){
		if(buf[0] != 0){
			n = strlen(buf)+1;
			memmove(buf, buf+n, sizeof(buf)-n);
		}
		if(buf[0] == 0){
			n = read(fd, buf, sizeof(buf)-1);
			if(n <= 0)
				break;
			buf[n-1] = 0;
			buf[n] = 0;
		}
		switch(buf[0]){
		default: continue;
		case 'k': s = buf+1; p = down+1; ke.down = 1; break;
		case 'K': s = down+1; p = buf+1; ke.down = 0; break;
		}
		while(*s != 0){
			s += chartorune(&r, s);
			if(utfrune(p, r) == nil){
				ke.r = r;
				if(send(kc, &ke) < 0)
					threadexits(nil);
			}
		}
		strcpy(down, buf);
	}
}

static void
timeproc(void *)
{
	int tdiv;
	vlong t, t0, dt, Δtc;

	tdiv = Te9 / Hz;
	t0 = nsec();
	for(;;){
		nbsendul(tmc, 0);
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

static void
initcl(void)
{
	if(initdraw(nil, nil, progname) < 0)
		sysfatal("initdraw: %r");
	initsnd();
	initimg();
	resetfb();
	if((reszc = chancreate(sizeof(int), 2)) == nil
	|| (kc = chancreate(sizeof(Kev), 20)) == nil
	|| (mc = chancreate(sizeof(Mev), 20)) == nil
	|| (tmc = chancreate(sizeof(ulong), 0)) == nil)
		sysfatal("chancreate: %r");
	if(proccreate(kproc, nil, 8192) < 0
	|| proccreate(mproc, nil, 8192) < 0
	|| proccreate(timeproc, nil, 8192) < 0)
		sysfatal("proccreate: %r");
}

static void
usage(void)
{
	fprint(2, "usage: %s [-D] [-P port] [-m map] [-n name] [-s scale] [-t speed] [-x netmtpt] [sys]\n", argv0);
	threadexits("usage");
}

void
threadmain(int argc, char **argv)
{
	int tv;
	Kev ke;
	Mev me;

	tv = Tfast;
	ARGBEGIN{
	case 'D': debug = 1; break;
	case 'P': lport = strtol(EARGF(usage()), nil, 0); break;
	case 'm': mapname = EARGF(usage()); break;
	case 'n': progname = EARGF(usage()); break;
	case 's':
		scale = strtol(EARGF(usage()), nil, 0);
		if(scale < 1)
			scale = 1;
		else if(scale > 16)
			scale = 16;
		break;
	case 't':
		tv = strtol(EARGF(usage()), nil, 0);
		if(tv < 1)
			tv = 1;
		else if(tv > 8)
			tv = 8;
		break;
	case 'x': netmtpt = EARGF(usage()); break;
	default: usage();
	}ARGEND
	//mainmem->flags |= POOL_PARANOIA | POOL_NOREUSE;
	if(dbname == nil)
		dbname = smprint("%s.db", progname);
	if(prefix == nil)
		prefix = smprint("/sys/games/lib/%s", progname);
	srand(time(nil));
	initfs();
	initsv(tv, *argv);
	initcl();
	enum{
		Aresize,
		Amouse,
		Akbd,
		Atic,
		Aend,
	};
	Alt a[] = {
		{reszc, nil, CHANRCV},
		{mc, &me, CHANRCV},
		{kc, &ke, CHANRCV},
		{tmc, nil, CHANRCV},
		{nil, nil, CHANEND}
	};
	for(;;){
		switch(alt(a)){
		case Aresize:
			if(getwindow(display, Refnone) < 0)
				sysfatal("resize failed: %r");
			resetfb();
			break;
		case Amouse:
			qlock(&drawlock);	/* just for security */
			if(me.b & 1)
				select(me);
			if(me.b & 2)
				dopan(me.Δ);
			if(me.b & 4)
				move(me);
			qunlock(&drawlock);
			flushcl();
			break;
		case Akbd:
			if(ke.r == Kdel)
				threadexitsall(nil);
			if(!ke.down)
				continue;
			switch(ke.r){
			case KF|1: debugmap ^= 1; break;
			case ' ': sendpause(); break;
			}
			flushcl();
			break;
		case Atic:
			updatefb();
			break;
		}
	}
}
