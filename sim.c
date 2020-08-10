#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

Team team[Nteam], *curteam;
int nteam;
int initres[Nresource], foodcap;

static Mobjl moving0 = {.l = &moving0, .lp = &moving0}, *moving = &moving0;

static Mobjl *
linkmobj(Mobjl *l, Mobj *mo, Mobjl *p)
{
	if(p == nil)
		p = emalloc(sizeof *p);
	p->mo = mo;
	p->l = l->l;
	p->lp = l;
	l->l->lp = p;
	l->l = p;
	return p;
}

static void
unlinkmobj(Mobjl *ml)
{
	if(ml == nil || ml->l == nil || ml->lp == nil)
		return;
	ml->lp->l = ml->l;
	ml->l->lp = ml->lp;
	ml->lp = ml->l = nil;
}

void
linktomap(Mobj *mo)
{
	Map *m;

	m = map + mo->y * mapwidth + mo->x;
	mo->mapp = linkmobj(mo->f & Fair ? m->ml.lp : &m->ml, mo, mo->mapp);
}

static void
resetcoords(Mobj *mo)
{
	markmobj(mo, 0);
	mo->subpx = mo->px << Subpxshift;
	mo->subpy = mo->py << Subpxshift;
	markmobj(mo, 1);
}

static int
facemobj(Point p, Mobj *mo)
{
	int dx, dy;
	double vx, vy, θ, d;

	dx = p.x - mo->x;
	dy = p.y - mo->y;
	d = sqrt(dx * dx + dy * dy);
	vx = dx / d;
	vy = dy / d;
	mo->u = vx;
	mo->v = vy;
	θ = atan2(vy, vx) + PI / 2;
	if(θ < 0)
		θ += 2 * PI;
	else if(θ >= 2 * PI)
		θ -= 2 * PI;
	return (θ / (2 * PI) * 360) / (90. / (Nrot / 4));
}

static void
freemove(Mobj *mo)
{
	unlinkmobj(mo->movingp);
	mo->pathp = nil;
	mo->pics = &mo->o->pidle;
	resetcoords(mo);
}

static void
nextmove(Mobj *mo)
{
	int Δθ;

	resetcoords(mo);
	Δθ = facemobj(*mo->pathp, mo) - mo->θ;
	if(Δθ <= -Nrot / 2)
		Δθ += Nrot;
	else if(Δθ >= Nrot / 2)
		Δθ -= Nrot;
	mo->Δθ = Δθ;
	mo->speed = mo->o->speed;
}

static int
repath(Point p, Mobj *mo)
{
	freemove(mo);
	mo->target = p;
	if(findpath(p, mo) < 0){
		mo->θ = facemobj(p, mo);
		return -1;
	}
	mo->movingp = linkmobj(moving, mo, mo->movingp);
	mo->pathp = mo->paths;
	mo->pics = mo->o->pmove.p != nil ? &mo->o->pmove : &mo->o->pidle;
	nextmove(mo);
	return 0;
}

int
moveone(Point p, Mobj *mo, Mobj *block)
{
	if(mo->o->speed == 0){
		dprint("move: obj %s can't move\n", mo->o->name);
		return -1;
	}
	setgoal(&p, mo, block);
	if(repath(p, mo) < 0){
		dprint("move to %d,%d: %r\n", p.x, p.y);
		return -1;
	}
	return 0;
}

int
spawn(int x, int y, Obj *o, int n)
{
	Mobj *mo;

	if((mo = mapspawn(x, y, o)) == nil)
		return -1;
	mo->team = n;
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

	if(mo->Δθ < 0)
		Δθ = mo->Δθ < -4 ? -4 : mo->Δθ;
	else
		Δθ = mo->Δθ > 4 ? 4 : mo->Δθ;
	mo->θ = mo->θ + Δθ & Nrot - 1;
	mo->Δθ -= Δθ;
}

static int
trymove(Mobj *mo)
{
	int x, y, sx, sy, Δx, Δy, Δu, Δv, Δrx, Δry, Δpx, Δpy;

	markmobj(mo, 0);
	sx = mo->subpx;
	sy = mo->subpy;
	Δu = mo->u * (1 << Subpxshift);
	Δv = mo->v * (1 << Subpxshift);
	Δx = abs(Δu);
	Δy = abs(Δv);
	Δrx = Δx * mo->speed;
	Δry = Δy * mo->speed;
	Δpx = abs((mo->pathp->x * Tlsubwidth << Subpxshift) - sx);
	Δpy = abs((mo->pathp->y * Tlsubwidth << Subpxshift) - sy);
	if(Δpx < Δrx)
		Δrx = Δpx;
	if(Δpy < Δry)
		Δry = Δpy;
	while(Δrx > 0 || Δry > 0){
		x = mo->x;
		y = mo->y;
		if(Δrx > 0){
			sx += Δu;
			Δrx -= Δx;
			if(Δrx < 0)
				sx += mo->u < 0 ? -Δrx : Δrx;
			x = (sx >> Subpxshift) + ((sx & Subpxmask) != 0);
			x /= Tlsubwidth;
		}
		if(Δry > 0){
			sy += Δv;
			Δry -= Δy;
			if(Δry < 0)
				sy += mo->v < 0 ? -Δry : Δry;
			y = (sy >> Subpxshift) + ((sy & Subpxmask) != 0);
			y /= Tlsubwidth;
		}
		if(isblocked(x, y, mo->o))
			goto end;
		/* disallow corner coasting */
		if(x != mo->x && y != mo->y
		&& (isblocked(x, mo->y, mo->o) || isblocked(mo->x, y, mo->o))){
			dprint("detected corner coasting %d,%d vs %d,%d\n",
				x, y, mo->x, mo->y);
			goto end;
		}
		mo->subpx = sx;
		mo->subpy = sy;
		mo->px = sx >> Subpxshift;
		mo->py = sy >> Subpxshift;
		mo->x = mo->px / Tlsubwidth;
		mo->y = mo->py / Tlsubheight;
	}
	markmobj(mo, 1);
	return 0;
end:
	werrstr("trymove: can't move to %d,%d", x, y);
	mo->subpx = mo->px << Subpxshift;
	mo->subpy = mo->py << Subpxshift;
	markmobj(mo, 1);
	return -1;
}

static void
stepmove(Mobj *mo)
{
	int r, n;

	n = 0;
restart:
	n++;
	if(mo->Δθ != 0){
		tryturn(mo);
		return;
	}
	unlinkmobj(mo->mapp);
	r = trymove(mo);
	linktomap(mo);
	if(r < 0){
		if(n > 1){
			fprint(2, "stepmove: %s %#p bug inducing infinite loop!\n",
				mo->o->name, mo);
			return;
		}
		dprint("stepmove: failed to move: %r\n");
		if(repath(mo->target, mo) < 0){
			dprint("stepmove: %s %#p moving towards target: %r\n",
				mo->o->name, mo);
			return;
		}
		goto restart;
	}
	if(mo->x == mo->pathp->x && mo->y == mo->pathp->y){
		mo->pathp++;
		if(mo->pathp < mo->pathe){
			nextmove(mo);
			return;
		}else if(mo->x == mo->target.x && mo->y == mo->target.y){
			mo->npatherr = 0;
			freemove(mo);
			return;
		}
		dprint("stepmove: %s %#p reached final node, but not target\n",
			mo->o->name, mo);
		if(mo->goalblocked && isblocked(mo->target.x, mo->target.y, mo->o)){
			dprint("stepmove: %s %#p goal still blocked, stopping\n",
				mo->o->name, mo);
			freemove(mo);
			return;
		}
		if(mo->npatherr++ > 1
		|| repath(mo->target, mo) < 0){
			dprint("stepmove: %s %#p trying to find target: %r\n",
				mo->o->name, mo);
			mo->npatherr = 0;
			freemove(mo);
		}
	}
}

void
stepsim(void)
{
	Mobjl *ml, *oml;

	for(oml=moving->l, ml=oml->l; oml!=moving; oml=ml, ml=ml->l)
		stepmove(oml->mo);
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
