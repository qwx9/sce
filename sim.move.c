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
nextpathnode(Mobj *mo)
{
	resetcoords(mo);
	facegoal(*mo->pathp, mo);
}

static void
clearpath(Mobj *mo)
{
	mo->speed = 0.0;
	mo->pathp = nil;
	resetcoords(mo);
}

static void
cleanup(Mobj *mo)
{
	clearpath(mo);
	mo->target = (Point){0,0};
	mo->goalblocked = 0;
	mo->pathlen = 0.0;
	mo->npatherr = 0;
}

static void
movedone(Mobj *mo)
{
	dprint("%M successfully reached goal\n", mo);
	nextaction(mo);
}

static void
abortmove(Mobj *mo)
{
	werrstr("move aborted");
	abortcommands(mo);
}

static int
repath(Point p, Mobj *mo)
{
	clearpath(mo);
	mo->target = p;
	if(findpath(p, mo) < 0){
		mo->θ = facegoal(p, mo);
		return -1;
	}
	mo->pathp = mo->paths;
	nextpathnode(mo);
	return 0;
}

static void
accelerate(Mobj *mo)
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
			dprint("%M detected corner coasting at %d,%d\n",
				mo, x, y);
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

static int
nodereached(Mobj *mo)
{
	return mo->px == mo->pathp->x && mo->py == mo->pathp->y;
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
		dprint("%M stepmove: failed moving from %d,%d to %d,%d: %r\n",
			mo, mo->px, mo->py, mo->pathp->x, mo->pathp->y);
		if(repath(mo->target, mo) < 0){
			dprint("%M stepmove: failed moving towards target: %r\n", mo);
			abortcommands(mo);
			return;
		}
		nerr++;
		goto restart;
	}
	if(!nodereached(mo))
		return;
	mo->pathp++;
	if(mo->pathp < mo->pathe){
		nextpathnode(mo);
		return;
	}else if(mo->x == mo->target.x && mo->y == mo->target.y){
		movedone(mo);
		return;
	}
	dprint("%M stepmove: reached final node, but not target\n", mo);
	if(mo->goalblocked && isblocked(mo->target.x, mo->target.y, mo->o)){
		dprint("%M stepmove: goal still blocked, stopping\n", mo);
		abortmove(mo);
		return;
	}
	dprint("%M stepmove: trying again\n", mo);
	if(mo->npatherr++ > 1 || repath(mo->target, mo) < 0){
		dprint("%M stepmove: still can't reach target: %r\n", mo);
		abortmove(mo);
		return;
	}
}

static Action acts[] = {
	{
		.os = OSmove,
		.name = "moving",
		.stepfn = step,
		.cleanupfn = cleanup,
	},
	{
		.os = OSskymaybe,
	}
};

int
newmove(Mobj *mo)
{
	Point goal;
	Mobj *block;
	Command *c;

	c = mo->cmds;
	goal = c->goal;
	block = nil;
	if(c->arg[0] >= 0 && (block = derefmobj(c->arg[0], c->arg[1])) == nil)
		return -1;
	setgoal(&goal, mo, block);
	if(repath(goal, mo) < 0)
		return -1;
	if(pushactions(mo, acts) < 0)
		return -1;
	return 0;
}

int
pushmovecommand(Point goal, Mobj *mo, Mobj *block)
{
	Command *c;

	if((c = pushcommand(mo)) == nil){
		fprint(2, "pushmovecommand: %r\n");
		return -1;
	}
	c->os = OSmove;
	c->name = "move";
	c->initfn = newmove;
	c->goal = goal;
	if(block != nil){
		c->arg[0] = block->idx;
		c->arg[1] = block->uuid;
	}else
		c->arg[0] = -1;
	return 0;
}
