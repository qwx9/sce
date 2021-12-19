#include <u.h>
#include <libc.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

static double
facegoal(Mobj *mo, Point p)
{
	int dx, dy;
	double vx, vy, d, θ, θ256, Δθ;

	dx = p.x - mo->x;
	dy = p.y - mo->y;
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

	p = addpt(tgt->Point, Pt(tgt->o->w / 2, tgt->o->h / 2));
	return facegoal(mo, p);
}

static void
nextpathnode(Mobj *mo)
{
	snaptomapgrid(mo);
	facegoal(mo, *mo->path.step);
}

static void
clearpath(Mobj *mo)
{
	snaptomapgrid(mo);
	mo->speed = 0.0;
	mo->path.step = nil;
	clearvec(&mo->path.moves, sizeof *mo->path.step);
}

static void
cleanup(Mobj *mo)
{
	clearpath(mo);
	mo->path.target = ZP;
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

static int
repath(Mobj *mo, Point p)
{
	clearpath(mo);
	mo->path.target = p;
	if(findpath(mo, p) < 0){
		mo->θ = facegoal(mo, p);
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
	mo->speed /= 8;
}

static int
trymove(Mobj *mo)
{
	int Δx, Δy, Δu, Δv, Δrx, Δry, Δpx, Δpy;
	Point p, from, sub;

	markmobj(mo, 0);
	from = mo->Point;
	sub = mo->sub;
	Δu = mo->u * (1 << Subshift);
	Δv = mo->v * (1 << Subshift);
	Δx = abs(Δu);
	Δy = abs(Δv);
	Δrx = fabs(mo->u * mo->speed) * (1 << Subshift);
	Δry = fabs(mo->v * mo->speed) * (1 << Subshift);
	Δpx = abs((mo->path.step->x << Subshift) - sub.x);
	Δpy = abs((mo->path.step->y << Subshift) - sub.y);
	if(Δpx < Δrx)
		Δrx = Δpx;
	if(Δpy < Δry)
		Δry = Δpy;
	while(Δrx > 0 || Δry > 0){
		p = mo->Point;
		if(Δrx > 0){
			sub.x += Δu;
			Δrx -= Δx;
			if(Δrx < 0)
				sub.x += mo->u < 0 ? -Δrx : Δrx;
			p.x = (sub.x >> Subshift) + ((sub.x & Submask) != 0);
		}
		if(Δry > 0){
			sub.y += Δv;
			Δry -= Δy;
			if(Δry < 0)
				sub.y += mo->v < 0 ? -Δry : Δry;
			p.y = (sub.y >> Subshift) + ((sub.y & Submask) != 0);
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
		setsubpos(mo, sub);
	}
	markmobj(mo, 1);
	mo->path.dist -= eucdist(mo->Point, from);
	return 0;
end:
	werrstr("trymove: can't move to %P", p);
	setpos(mo, mo->Point);
	markmobj(mo, 1);
	mo->path.dist -= eucdist(mo->Point, from);
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
	return eqpt(mo->Point, *mo->path.step);
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
		dprint("%M stepmove: failed moving from %P to %P: %r\n",
			mo, mo->Point, *mo->path.step);
		if(repath(mo, mo->path.target) < 0){
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
	if(mo->path.nerr++ > 1 || repath(mo, mo->path.target) < 0){
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
	setgoal(mo, &goal, c->target1);
	if(repath(mo, goal) < 0)
		return -1;
	if(eqpt(mo->Point, goal)){
		mo->state = OSskymaybe;
		return 0;
	}
	c->stepfn = step;
	mo->state = OSmove;
	return 0;
}

int
pushmovecommand(Mobj *mo, Point goal, Mobj *target)
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
