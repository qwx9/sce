#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

Team team[Nteam], *curteam;
int nteam;
int initres[Nresource], foodcap;

static Lobj vlist = {.lo = &vlist, .lp = &vlist };

/* FIXME: networking: recvbuf and sendbuf for accumulating messages to flush
 * to all clients */
/* FIXME: acceleration, deceleration, turning speed (360°) */
/* FIXME: minerals: 4 spaces in every direction forbidding cc placement */
/* FIXME: resource tiles: take 2x1 tiles, drawn on top of rest
 *	-> actual (immutable) object, not terrain (remove resource= from
 *	   terrain shit, move to objects)
 *	-> minerals: min0?.grp, three types depending on abundance, with
 *	   several variants each
 *	-> Geyser.grp (terrain pal?) */
/* FIXME: rip mineral sprites and implement terrain objects layer */
/* FIXME: buildings are aligned to map grid, while units are aligned to
 * path grid -> building placement uses Map, not Path */
/* FIXME: verify that path node centering does in fact work correctly, esp
 * viz blockmap and .subp; if it does, do centering at spawn */
/* FIXME: fix land spawning to always work if adjacent space can be found */
/* FIXME: once spawning is fixed, remove race-specific mentions and units from
 * map spec, having only starts instead, resolving to a race's cc and spawning
 * 4 workers by default (marked in db) */

/* FIXME:
 - networking
 	. server: spawn server + local client (by default)
 	. client: connect to server
 	. both initialize a simulation, but only the server modifies state
 - command line: choose whether or not to spawn a server and/or a client
   (default: both)
 - always spawn a server, always initialize loopback channel to it; client
   should do the same amount of work but the server always has the last word
   	. don't systematically announce a port
 - client: join observers on connect
 - program a lobby for both client and server
 	. server: kick/ban ip's, rate limit, set teams and rules
 	. master client -> server priviledges
 	. master client == local client
 	. if spawning a server only, local client just implements the lobby
 	  and a console interface, same as lobby, for controlling the server
 	. client: choose slot
 */
/* FIXME: db: build tree specification */

static Lobj *
lalloc(Mobj *mo, ulong sz)
{
	Lobj *lo, *l;

	lo = emalloc(sz * sizeof *lo);
	for(l=lo; sz>0; sz--, l++)
		l->mo = mo;
	return lo;
}

void
llink(Lobj *lo, Lobj *lp)
{
	lo->lo = lp->lo;
	lo->lp = lp;
	lp->lo->lp = lo;
	lp->lo = lo;
}

void
lunlink(Lobj *lo)
{
	lo->lp->lo = lo->lo;
	lo->lo->lp = lo->lp;
}

static void
lfree(Lobj *lo)
{
	lunlink(lo);
	free(lo);
}

static void
freepath(Mobj *mo)
{
	if(mo->path == nil)
		return;
	free(mo->path);
	mo->path = mo->pathp = mo->pathe = nil;
	lunlink(mo->vl);
}

static void
nextpath(Mobj *mo)
{
	int Δθ, vx, vy;
	double θ, l;
	Point p;

	p = *mo->pathp++;
	vx = p.x - mo->p.x;
	vy = p.y - mo->p.y;
	l = sqrt(vx * vx + vy * vy);
	mo->vx = vx / l;
	mo->vy = vy / l;
	mo->vv = mo->o->speed;
	θ = atan2(mo->vy, mo->vx) + PI / 2;
	if(θ < 0)
		θ += 2 * PI;
	else if(θ >= 2 * PI)
		θ -= 2 * PI;
	Δθ = (θ / (2*PI) * 360) / (90. / (Nrot/4)) - mo->θ;
	if(Δθ <= -Nrot / 2)
		Δθ += Nrot;
	else if(Δθ >= Nrot / 2)
		Δθ -= Nrot;
	mo->Δθ = Δθ;
}

static int
repath(Mobj *mo, Point *p)
{
	freepath(mo);
	if(findpath(mo, p) < 0){
		fprint(2, "repath: move to %d,%d: %r\n", p->x, p->y);
		return -1;
	}
	if(mo->vl == nil)
		mo->vl = lalloc(mo, 1);
	llink(mo->vl, vlist.lp);
	mo->pics = mo->o->pmove.p != nil ? &mo->o->pmove : &mo->o->pidle;
	nextpath(mo);
	return 0;
}

static int
canmove(int x, int y, Mobj **op)
{
	/* FIXME: attempt to move in the given direction even if impassible */
	USED(x, y, op);
	return 1;
}

int
move(int x, int y, Mobj **op)
{
	Mobj *mo, **mp;
	Point p;

	if(!canmove(x, y, op))
		return -1;
	for(mp=op; (mo=*mp)!=nil; mp++){
		/* FIXME: offset sprite size */
		p = (Point){x & ~Tlsubmask, y & ~Tlsubmask};
		if(mo->o->speed != 0 && repath(mo, &p) < 0)
			continue;
	}
	return 0;
}

/* FIXME: we're not actually spawning a unit at a location, are we? it's a
 * unit or structure that spawns a unit, the placement is determined based on
 * available space -> the spawning mechanism works the same; initial units are
 * always the same and are spawned in the same manner */
static int
canspawn(Map *m, Mobj *mo)
{
	Point p;

	/* FIXME: find space, unless not a unit */
	p.x = m->tx * (Tlwidth / Tlsubwidth);
	p.y = m->ty * (Tlheight / Tlsubheight);
	if(isblocked(&p, mo)){
		werrstr("no available space");
		return 0;
	}
	return 1;
}

int
spawn(Map *m, Obj *o, int n)
{
	Mobj *mo;

	mo = emalloc(sizeof *mo);
	mo->o = o;
	mo->p.x = m->x;
	mo->p.y = m->y;
	if(!canspawn(m, mo)){
		free(mo);
		return -1;
	}
	mo->x = m->x / Tlsubwidth;
	mo->y = m->y / Tlsubheight;
	mo->team = n;
	mo->f = o->f;
	mo->hp = o->hp;
	mo->zl = lalloc(mo, 1);
	llink(mo->zl, &m->lo);
	mo->blk = lalloc(mo, mo->o->w * (Tlwidth / Tlsubwidth) * mo->o->h * (Tlheight / Tlsubheight));
	setblk(mo, 0);
	mo->pics = &mo->o->pidle;
	if(mo->f & Fbuild)
		team[n].nbuild++;
	else
		team[n].nunit++;
	return 0;
}

static void
tryturn(Mobj *mo)
{
	int Δθ;

	Δθ = mo->Δθ < 0 ? -1 : 1;
	mo->θ = mo->θ + Δθ & Nrot - 1;
	mo->Δθ -= Δθ;
}

static int
trymove(Mobj *mo)
{
	int Δr, sx, sy;
	Point subΔr, subp, p, pp;

	subΔr.x = mo->vx * mo->vv * (1 << Tlshift);
	subΔr.y = mo->vy * mo->vv * (1 << Tlshift);
	sx = subΔr.x < 0 ? -1 : 1;
	sy = subΔr.y < 0 ? -1 : 1;
	subΔr.x *= sx;
	subΔr.y *= sy;
	Δr = sx * (mo->pathp[-1].x - mo->p.x << Tlshift) - mo->subp.x;
	if(subΔr.x > Δr)
		subΔr.x = Δr;
	Δr = sy * (mo->pathp[-1].y - mo->p.y << Tlshift) - mo->subp.y;
	if(subΔr.y > Δr)
		subΔr.y = Δr;
	while(subΔr.x > 0 || subΔr.y > 0){
		p = mo->p;
		subp = mo->subp;
		if(subΔr.x > 0){
			Δr = (Tlsubwidth << Tlshift) - subp.x;
			if(Δr <= subΔr.x){
				p.x += sx * Tlsubwidth;
				subp.x = 0;
			}else{
				subp.x += subΔr.x;
				p.x += sx * (subp.x >> Tlshift);
				subp.x &= (1 << Tlshift) - 1;
			}
			subΔr.x -= Δr;
		}
		if(subΔr.y > 0){
			Δr = (Tlsubheight << Tlshift) - subp.y;
			if(Δr <= subΔr.y){
				p.y += sy * Tlsubheight;
				subp.y = 0;
			}else{
				subp.y += subΔr.y;
				p.y += sy * (subp.y >> Tlshift);
				subp.y &= (1 << Tlshift) - 1;
			}
			subΔr.y -= Δr;
		}
		pp.x = p.x / Tlsubwidth;
		pp.y = p.y / Tlsubheight;
		if(mo->x != pp.x || mo->y != pp.y){
			if(isblocked(&pp, mo))
				return -1;
			setblk(mo, 1);
			mo->x = pp.x;
			mo->y = pp.y;
			setblk(mo, 0);
		}
		mo->subp = subp;
		mo->p = p;
	}
	return 0;
}

static void
stepmove(Mobj *mo)
{
	Point pp;
	Map *m, *m´;

	/* FIXME: constant turn speed for now, but is it always so? */
	/* FIXME: too slow */
	if(mo->Δθ != 0){
		tryturn(mo);
		return;
	}
	m = map + mo->y / (Tlheight / Tlsubheight) * mapwidth
		+ mo->x / (Tlwidth / Tlsubwidth);
	/* FIXME: update speed (based on range from src and to target?) */
	/* FIXME: update speed: accel, decel, turning speed */
	if(trymove(mo) < 0){
		mo->vv = 0.0;
		pp = mo->pathe[-1];
		fprint(2, "mo %#p blocked at %d,%d goal %d,%d\n", mo, mo->x * Tlsubwidth, mo->y * Tlsubheight, mo->pathp[-1].x, mo->pathp[-1].y);
		/* FIXME: if path now blocked, move towards target anyway */
		repath(mo, &pp);
		return;
	}
	if(mo->p.x == mo->pathp[-1].x && mo->p.y == mo->pathp[-1].y){
		if(mo->pathp < mo->pathe)
			nextpath(mo);
		else{
			freepath(mo);
			mo->pics = &mo->o->pidle;
		}
	}
	m´ = map + mo->y / (Tlheight / Tlsubheight) * mapwidth
		+ mo->x / (Tlwidth / Tlsubwidth);
	if(m != m´){
		lunlink(mo->zl);
		llink(mo->zl, &m´->lo);
	}
}

void
stepsim(void)
{
	Lobj *lo;

	for(lo=vlist.lo; lo!=&vlist; lo=lo->lo)
		stepmove(lo->mo);
}

static void
initsim(void)
{
	Team *t;

	if(nteam < 2)
		sysfatal("initgame: the only winning move is not to play");
	for(t=team; t<=team+nteam; t++)
		memcpy(t->r, initres, sizeof initres);
}

void
initsv(void)
{
	initsim();
	listennet();
}
