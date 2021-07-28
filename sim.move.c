#include <u.h>
#include <libc.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

static Mobjl moving0 = {.l = &moving0, .lp = &moving0}, *moving = &moving0;

void
linktomap(Mobj *mo)
{
	Map *m;

	m = map + mo->y / Node2Tile * mapwidth + mo->x / Node2Tile;
	mo->mobjl = linkmobj(mo->o->f & Fair ? m->ml.lp : &m->ml, mo, mo->mobjl);
}

static void
resetcoords(Mobj *mo)
{
	markmobj(mo, 0);
	mo->subpx = mo->px << Subpxshift;
	mo->subpy = mo->py << Subpxshift;
	markmobj(mo, 1);
}

static double
facemobj(Point p, Mobj *mo)
{
	int dx, dy;
	double vx, vy, d, θ, θ256, Δθ;

	dx = p.x - mo->px;
	dy = p.y - mo->py;
	d = sqrt(dx * dx + dy * dy);
	vx = dx / d;
	vy = dy / d;
	/* angle in radians [0;2π[ with 0 facing north */
	θ = atan2(vy, vx) + PI / 2;
	if(θ < 0)
		θ += 2 * PI;
	else if(θ >= 2 * PI)
		θ -= 2 * PI;
	/* movement calculations use values in [0;256[, drawing in [0;32[ */
	θ256 = θ * 256.0 / (2 * PI);
	mo->u = vx;
	mo->v = vy;
	Δθ = θ256 - mo->θ;
	if(Δθ <= -256 / 2)
		Δθ += 256;
	else if(Δθ >= 256 / 2)
		Δθ -= 256;
	mo->Δθs = Δθ < 0 ? -1: 1;
	mo->Δθ = fabs(Δθ);
	return θ256;
}

static void
freemove(Mobj *mo)
{
	unlinkmobj(mo->movingp);
	mo->pathp = nil;
	mo->freezefrm = tc % mo->o->pics[mo->state][PTbase].nf;
	mo->state = OSidle;
	resetcoords(mo);
}

static void
nextmove(Mobj *mo)
{
	resetcoords(mo);
	facemobj(*mo->pathp, mo);
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
	mo->state = OSmove;
	nextmove(mo);
	return 0;
}

int
moveone(Point p, Mobj *mo, Mobj *block)
{
	setgoal(&p, mo, block);
	if(repath(p, mo) < 0){
		mo->speed = 0.0;
		dprint("move to %d,%d: %r\n", p.x, p.y);
		return -1;
	}
	return 0;
}

static int
tryturn(Mobj *mo)
{
	int r;
	double Δθ;

	r = 1;
	if(mo->Δθ <= mo->o->turn){
		r = 0;
		Δθ = mo->Δθ;
	}else
		Δθ = mo->o->turn;
	mo->θ += mo->Δθs * Δθ;
	if(mo->θ < 0)
		mo->θ += 256;
	else if(mo->θ >= 256)
		mo->θ -= 256;
	mo->Δθ -= Δθ;
	return r;
}

static void
updatespeed(Mobj *mo)
{
	if(1 + mo->pathlen < (mo->speed / 8) * (mo->speed / 8) / 2 / (mo->o->accel / 8)){
		mo->speed -= mo->o->accel;
		if(mo->speed < 0.0)
			mo->speed = 0.0;
	}else if(mo->speed < mo->o->speed){
		mo->speed += mo->o->accel;
		if(mo->speed > mo->o->speed)
			mo->speed = mo->o->speed;
	}
}

static int
trymove(Mobj *mo)
{
	int x, y, px, py, sx, sy, Δx, Δy, Δu, Δv, Δrx, Δry, Δpx, Δpy;
	double dx, dy;

	markmobj(mo, 0);
	px = mo->px;
	py = mo->py;
	sx = mo->subpx;
	sy = mo->subpy;
	Δu = mo->u * (1 << Subpxshift);
	Δv = mo->v * (1 << Subpxshift);
	Δx = abs(Δu);
	Δy = abs(Δv);
	Δrx = fabs(mo->u * mo->speed) * (1 << Subpxshift);
	Δry = fabs(mo->v * mo->speed) * (1 << Subpxshift);
	Δpx = abs((mo->pathp->x << Subpxshift) - sx);
	Δpy = abs((mo->pathp->y << Subpxshift) - sy);
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
			x /= Nodewidth;
		}
		if(Δry > 0){
			sy += Δv;
			Δry -= Δy;
			if(Δry < 0)
				sy += mo->v < 0 ? -Δry : Δry;
			y = (sy >> Subpxshift) + ((sy & Subpxmask) != 0);
			y /= Nodewidth;
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
		mo->x = mo->px / Nodewidth;
		mo->y = mo->py / Nodeheight;
	}
	markmobj(mo, 1);
	dx = mo->px - px;
	dx *= dx;
	dy = mo->py - py;
	dy *= dy;
	mo->pathlen -= sqrt(dx + dy) / Nodewidth;
	return 0;
end:
	werrstr("trymove: can't move to %d,%d", x, y);
	mo->subpx = mo->px << Subpxshift;
	mo->subpy = mo->py << Subpxshift;
	markmobj(mo, 1);
	dx = mo->px - px;
	dx *= dx;
	dy = mo->py - py;
	dy *= dy;
	mo->pathlen -= sqrt(dx + dy) / Nodewidth;
	return -1;
}

static int
domove(Mobj *mo)
{
	int r;

	updatespeed(mo);
	unlinkmobj(mo->mobjl);
	r = trymove(mo);
	linktomap(mo);
	return r;
}

static void
stepmove(Mobj *mo)
{
	int n;

	n = 0;
restart:
	n++;
	if(tryturn(mo))
		return;
	if(domove(mo) < 0){
		if(n > 1){
			fprint(2, "stepmove: %s %#p bug inducing infinite loop!\n",
				mo->o->name, mo);
			return;
		}
		dprint("stepmove: failed to move: %r\n");
		if(repath(mo->target, mo) < 0){
			dprint("stepmove: %s %#p moving towards target: %r\n",
				mo->o->name, mo);
			mo->speed = 0.0;
			return;
		}
		goto restart;
	}
	if(mo->px == mo->pathp->x && mo->py == mo->pathp->y){
		mo->pathp++;
		if(mo->pathp < mo->pathe){
			nextmove(mo);
			return;
		}else if(mo->x == mo->target.x && mo->y == mo->target.y){
			mo->npatherr = 0;
			mo->speed = 0.0;
			freemove(mo);
			return;
		}
		dprint("stepmove: %s %#p reached final node, but not target\n",
			mo->o->name, mo);
		if(mo->goalblocked && isblocked(mo->target.x, mo->target.y, mo->o)){
			dprint("stepmove: %s %#p goal still blocked, stopping\n",
				mo->o->name, mo);
			mo->speed = 0.0;
			freemove(mo);
			return;
		}
		if(mo->npatherr++ > 1
		|| repath(mo->target, mo) < 0){
			dprint("stepmove: %s %#p trying to find target: %r\n",
				mo->o->name, mo);
			mo->npatherr = 0;
			mo->speed = 0.0;
			freemove(mo);
		}
	}
}

void
updatemoves(void)
{
	Mobjl *ml, *oml;

	for(oml=moving->l, ml=oml->l; oml!=moving; oml=ml, ml=ml->l)
		stepmove(oml->mo);
}
