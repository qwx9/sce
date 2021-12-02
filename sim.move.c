#include <u.h>
#include <libc.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

void
linktomap(Mobj *mo)
{
	Map *m;

	m = map + mo->y / Node2Tile * mapwidth + mo->x / Node2Tile;
	mo->mapl = linkmobj(mo->o->f & Fair ? m->ml.lp : &m->ml, mo, mo->mapl);
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
facegoal(Point p, Mobj *mo)
{
	int dx, dy;
	double vx, vy, d, θ, θ256, Δθ;

	dx = p.x - mo->px;
	dy = p.y - mo->py;
	d = sqrt(dx * dx + dy * dy);
	if(d == 0.0)
		sysfatal("facegoal: %M → %P: moving in place, shouldn't happen", mo, p);
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

static double
facemobj(Mobj *mo, Mobj *tgt)
{
	Point p;

	p.x = tgt->px + tgt->o->w * Nodewidth / 2;
	p.y = tgt->py + tgt->o->h * Nodeheight / 2;
	return facegoal(p, mo);
}

static void
nextpathnode(Mobj *mo)
{
	resetcoords(mo);
	facegoal(*mo->path.step, mo);
}

static void
clearpath(Mobj *mo)
{
	resetcoords(mo);
	mo->speed = 0.0;
	mo->path.step = nil;
	clearvec(&mo->path.moves, sizeof *mo->path.step);
}

static void
cleanup(Mobj *mo)
{
	clearpath(mo);
	mo->path.target = (Point){0,0};
	mo->path.blocked = 0;
	mo->path.dist = 0.0;
	mo->path.nerr = 0;
}

static void
movereallyreallyreallydone(Mobj *mo)
{
	dprint("%M really really really successfully reached goal\n", mo);
	nextstate(mo);
}

static void	turnstep(Mobj*);

static void
movedone(Mobj *mo)
{
	Command *c;

	dprint("%M successfully reached goal\n", mo);
	c = mo->cmds;
	if(c->target1 == nil){
		nextstate(mo);
		return;
	}
	facemobj(mo, c->target1);
	c->stepfn = turnstep;
}

static void
abortmove(Mobj *mo)
{
	werrstr("move aborted");
	abortcommands(mo);
}

/* FIXME: kind of weird to mix up argument order,
 * mo should be first like elsewhere */
static int
repath(Point p, Mobj *mo)
{
	clearpath(mo);
	mo->path.target = p;
	if(findpath(p, mo) < 0){
		mo->θ = facegoal(p, mo);
		return -1;
	}
	nextpathnode(mo);
	return 0;
}

static void
accelerate(Mobj *mo)
{
	if(1 + mo->path.dist < (mo->speed / 8) * (mo->speed / 8) / 2 / (mo->o->accel / 8)){
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
	int px, py, sx, sy, Δx, Δy, Δu, Δv, Δrx, Δry, Δpx, Δpy;
	double dx, dy;
	Point p;

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
	Δpx = abs((mo->path.step->x << Subpxshift) - sx);
	Δpy = abs((mo->path.step->y << Subpxshift) - sy);
	if(Δpx < Δrx)
		Δrx = Δpx;
	if(Δpy < Δry)
		Δry = Δpy;
	while(Δrx > 0 || Δry > 0){
		p = mo->Point;
		if(Δrx > 0){
			sx += Δu;
			Δrx -= Δx;
			if(Δrx < 0)
				sx += mo->u < 0 ? -Δrx : Δrx;
			p.x = (sx >> Subpxshift) + ((sx & Subpxmask) != 0);
			p.x /= Nodewidth;
		}
		if(Δry > 0){
			sy += Δv;
			Δry -= Δy;
			if(Δry < 0)
				sy += mo->v < 0 ? -Δry : Δry;
			p.y = (sy >> Subpxshift) + ((sy & Subpxmask) != 0);
			p.y /= Nodewidth;
		}
		if(isblocked(p, mo->o))
			goto end;
		/* disallow corner coasting */
		if(p.x != mo->x && p.y != mo->y
		&& (isblocked(Pt(p.x, mo->y), mo->o)
		|| isblocked(Pt(mo->x, p.y), mo->o))){
			dprint("%M detected corner coasting at %P\n", mo, p);
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
	mo->path.dist -= sqrt(dx + dy) / Nodewidth;
	return 0;
end:
	werrstr("trymove: can't move to %P", p);
	mo->subpx = mo->px << Subpxshift;
	mo->subpy = mo->py << Subpxshift;
	markmobj(mo, 1);
	dx = mo->px - px;
	dx *= dx;
	dy = mo->py - py;
	dy *= dy;
	mo->path.dist -= sqrt(dx + dy) / Nodewidth;
	return -1;
}

static int
continuemove(Mobj *mo)
{
	int r;

	accelerate(mo);
	unlinkmobj(mo->mapl);
	r = trymove(mo);
	linktomap(mo);
	return r;
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
turnstep(Mobj *mo)
{
	if(!tryturn(mo))
		movereallyreallyreallydone(mo);
}

static int
nodereached(Mobj *mo)
{
	return mo->px == mo->path.step->x && mo->py == mo->path.step->y;
}

static void
step(Mobj *mo)
{
	int nerr;

	nerr = 0;
restart:
	if(tryturn(mo))
		return;
	if(continuemove(mo) < 0){
		if(nerr > 1){
			fprint(2, "%M stepmove: bug: infinite loop!\n", mo);
			return;
		}
		dprint("%M stepmove: failed moving from %d,%d to %P: %r\n",
			mo, mo->px, mo->py, *mo->path.step);
		if(repath(mo->path.target, mo) < 0){
			dprint("%M stepmove: failed moving towards target: %r\n", mo);
			abortcommands(mo);
			return;
		}
		nerr++;
		goto restart;
	}
	if(!nodereached(mo))
		return;
	mo->path.step--;
	if(mo->path.step >= mo->path.moves.p){
		nextpathnode(mo);
		return;
	}else if(eqpt(mo->Point, mo->path.target)){
		movedone(mo);
		return;
	}
	/* FIXME: this is a shitty hack, see notes */
	if(isnextto(mo, mo->cmds[0].target1)){
		dprint("%M stepmove: next to target, stopping\n", mo);
		movedone(mo);
		return;
	}
	dprint("%M stepmove: reached final node, but not target\n", mo);
	if(mo->path.blocked && isblocked(mo->path.target, mo->o)){
		dprint("%M stepmove: goal still blocked, stopping\n", mo);
		abortmove(mo);
		return;
	}
	dprint("%M stepmove: trying again\n", mo);
	if(mo->path.nerr++ > 1 || repath(mo->path.target, mo) < 0){
		dprint("%M stepmove: still can't reach target: %r\n", mo);
		abortmove(mo);
		return;
	}
}

int
pushmove(Mobj *mo)
{
	Point goal;
	Command *c;

	c = mo->cmds;
	c->cleanupfn = cleanup;
	goal = c->goal;
	setgoal(&goal, mo, c->target1);
	if(repath(goal, mo) < 0)
		return -1;
	if(eqpt(goal, mo->Point)){
		mo->state = OSskymaybe;
		return 0;
	}
	c->stepfn = step;
	mo->state = OSmove;
	return 0;
}

int
pushmovecommand(Point goal, Mobj *mo, Mobj *target)
{
	Command *c;

	if((c = pushcommand(mo)) == nil){
		fprint(2, "pushmovecommand: %r\n");
		return -1;
	}
	c->name = "move";
	c->initfn = pushmove;
	c->goal = goal;
	c->target1 = target;
	c->nextfn = nil;
	return 0;
}
