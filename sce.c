#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <mouse.h>
#include <keyboard.h>
#include <pool.h>
#include "dat.h"
#include "fns.h"

extern Point pan;
void	select(Point, int);

mainstacksize = 16*1024;

char *progname = "sce", *dbname, *prefix, *mapname = "map1.db";
int clon;
vlong tc;

typedef struct Kev Kev;
typedef struct Mev Mev;
struct Kev{
	int down;
	Rune r;
};
struct Mev{
	Point;
	int dx;
	int dy;
	int b;
};

enum{
	Te9 = 1000000000,
	Te6 = 1000000,
	Tfast = 6,
};
static int tv = Tfast, tdiv;
static vlong Δtc;
static int pause;
static Channel *reszc, *kc, *mc;

char *
estrdup(char *s)
{
	if((s = strdup(s)) == nil)
		sysfatal("estrdup: %r");
	setmalloctag(s, getcallerpc(&s));
	return s;
}

void *
emalloc(ulong n)
{
	void *p;

	if((p = mallocz(n, 1)) == nil)
		sysfatal("emalloc: %r");
	setmalloctag(p, getcallerpc(&n));
	return p;
}

vlong
flen(int fd)
{
	vlong l;
	Dir *d;

	if((d = dirfstat(fd)) == nil) 
		sysfatal("flen: %r");
	l = d->length;
	free(d);
	return l;
}

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
			m.dx = m.x - om.x;
			m.dy = m.y - om.y;
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
quit(void)
{
	packcl("u", Tquit);
	flushcl();
	threadexitsall(nil);
}

static void
input(void)
{
	Kev ke;
	Mev me;

	if(nbrecv(reszc, nil) != 0){
		if(getwindow(display, Refnone) < 0)
			sysfatal("resize failed: %r");
		resetfb();
	}
	while(nbrecv(mc, &me) > 0){
		if(me.b & 5)
			select(me, me.b);
		if(me.b & 2)
			dopan(me.dx, me.dy);
	}
	while(nbrecv(kc, &ke) > 0){
		if(!ke.down)
			continue;
		switch(ke.r){
		case ' ':
			pause ^= 1;
			break;
		case '=':
			if(scale < 16){
				scale++;
				resetfb();
			}
			break;
		case '-':
			if(scale > 1){
				scale--;
				resetfb();
			}
			break;
		case Kprint: scale = 1; pan = ZP; resetfb(); break;
		case Kdel: quit(); break;
		}
	}
}

static void
stepcl(void)
{
	if(!clon)
		return;
	input();
	flushcl();
	redraw();
	drawfb();
	stepsnd();
}

static void
initcl(void)
{
	clon = 1;
	if(initdraw(nil, nil, progname) < 0)
		sysfatal("initdraw: %r");
	if((reszc = chancreate(sizeof(int), 2)) == nil
	|| (kc = chancreate(sizeof(Kev), 20)) == nil
	|| (mc = chancreate(sizeof(Mev), 20)) == nil)
		sysfatal("chancreate: %r");
	if(proccreate(kproc, nil, 8192) < 0
	|| proccreate(mproc, nil, 8192) < 0)
		sysfatal("proccreate: %r");
	initsnd();
	initimg();
	resetfb();
}

static void
step(void)
{
	stepnet();
	stepcl();
	while(!pause && Δtc-- > 0)
		stepsim();
}

static void
usage(void)
{
	fprint(2, "usage: %s [-l port] [-m map] [-n name] [-t speed] [-x netmtpt] [sys]\n", argv0);
	threadexits("usage");
}

void
threadmain(int argc, char **argv)
{
	vlong t, t0, dt;

	ARGBEGIN{
	case 'l': lport = strtol(EARGF(usage()), nil, 0); break;
	case 'm': mapname = EARGF(usage()); break;
	case 'n': progname = EARGF(usage()); break;
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
	init();
	initsv();
	initcl();
	joinnet(*argv);
	tdiv = Te9 / (tv * 3);
	Δtc = 1;
	t0 = nsec();
	for(;;){
		step();
		t = nsec();
		Δtc = (t - t0) / tdiv;
		if(Δtc <= 0)
			Δtc = 1;
		else if(Δtc > 1){
			t0 += (vlong)(Δtc - 1) * tdiv;
			Δtc = 1;
		}
		t0 += Δtc * tdiv;
		tc += Δtc;
		dt = (t0 - t) / Te6;
		if(dt > 0)
			sleep(dt);
	}
}
