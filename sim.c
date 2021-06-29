#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

Team teams[Nteam], *curteam;
int nteam;
int initres[Nresource], foodcap;

static Munitl moving0 = {.l = &moving0, .lp = &moving0}, *moving = &moving0;

static Munitl *
linkmunit(Munitl *l, Munit *mu, Munitl *p)
{
	if(p == nil)
		p = emalloc(sizeof *p);
	p->mu = mu;
	p->l = l->l;
	p->lp = l;
	l->l->lp = p;
	l->l = p;
	return p;
}

static void
unlinkmunit(Munitl *ml)
{
	if(ml == nil || ml->l == nil || ml->lp == nil)
		return;
	ml->lp->l = ml->l;
	ml->l->lp = ml->lp;
	ml->lp = ml->l = nil;
}

void
linktomap(Munit *mu)
{
	Map *m;

	m = map + mu->y / Node2Tile * mapwidth + mu->x / Node2Tile;
	mu->munitl = linkmunit(mu->f & Fair ? m->ml.lp : &m->ml, mu, mu->munitl);
}

static void
refmunit(Munit *mu)
{
	int n, i;
	Team *t;

	t = teams + mu->team;
	if(mu->f & Fbuild)
		t->nbuild++;
	else
		t->nunit++;
	n = t->firstempty;
	if(n == t->sz){
		t->mu = erealloc(t->mu, (t->sz + 32) * sizeof *t->mu, t->sz * sizeof *t->mu);
		t->sz += 32;
	}
	t->mu[n] = mu;
	mu->idx = mu->team << Teamshift | n;
	for(i=t->firstempty+1; i<t->sz; i++)
		if(t->mu[i] == nil)
			break;
	t->firstempty = i;
}

static void
resetcoords(Munit *mu)
{
	markmunit(mu, 0);
	mu->subpx = mu->px << Subpxshift;
	mu->subpy = mu->py << Subpxshift;
	markmunit(mu, 1);
}

static double
facemunit(Point p, Munit *mu)
{
	int dx, dy;
	double vx, vy, d, θ, θ256, Δθ;

	dx = p.x - mu->px;
	dy = p.y - mu->py;
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
	mu->vx→ = vx;
	mu->vy→ = vy;
	Δθ = θ256 - mu->θ;
	if(Δθ <= -256 / 2)
		Δθ += 256;
	else if(Δθ >= 256 / 2)
		Δθ -= 256;
	mu->Δθs = Δθ < 0 ? -1: 1;
	mu->Δθ = fabs(Δθ);
	return θ256;
}

static void
freemove(Munit *mu)
{
	unlinkmunit(mu->movingp);
	mu->pathp = nil;
	mu->freezefrm = tc % mu->u->pics[mu->state][PTbase].nf;
	mu->state = OSidle;
	resetcoords(mu);
}

static void
nextmove(Munit *mu)
{
	resetcoords(mu);
	facemunit(*mu->pathp, mu);
}

static int
repath(Point p, Munit *mu)
{
	freemove(mu);
	mu->target = p;
	if(findpath(p, mu) < 0){
		mu->θ = facemunit(p, mu);
		return -1;
	}
	mu->movingp = linkmunit(moving, mu, mu->movingp);
	mu->pathp = mu->paths;
	mu->state = OSmove;
	nextmove(mu);
	return 0;
}

int
moveone(Point p, Munit *mu, Munit *block)
{
	if(mu->u->speed == 0){
		dprint("move: unit %s can't move\n", mu->u->name);
		return -1;
	}
	setgoal(&p, mu, block);
	if(repath(p, mu) < 0){
		mu->speed = 0.0;
		dprint("move to %d,%d: %r\n", p.x, p.y);
		return -1;
	}
	return 0;
}

int
spawn(int x, int y, Unit *u, int n)
{
	Munit *mu;

	if((mu = mapspawn(x, y, u)) == nil)
		return -1;
	mu->team = n;
	mu->state = OSidle;
	refmunit(mu);
	return 0;
}

static int
tryturn(Munit *mu)
{
	int r;
	double Δθ;

	r = 1;
	if(mu->Δθ <= mu->u->turn){
		r = 0;
		Δθ = mu->Δθ;
	}else
		Δθ = mu->u->turn;
	mu->θ += mu->Δθs * Δθ;
	if(mu->θ < 0)
		mu->θ += 256;
	else if(mu->θ >= 256)
		mu->θ -= 256;
	mu->Δθ -= Δθ;
	return r;
}

static void
updatespeed(Munit *mu)
{
	if(1 + mu->pathlen < (mu->speed / 8) * (mu->speed / 8) / 2 / (mu->u->accel / 8)){
		mu->speed -= mu->u->accel;
		if(mu->speed < 0.0)
			mu->speed = 0.0;
	}else if(mu->speed < mu->u->speed){
		mu->speed += mu->u->accel;
		if(mu->speed > mu->u->speed)
			mu->speed = mu->u->speed;
	}
}

static int
trymove(Munit *mu)
{
	int x, y, px, py, sx, sy, Δx, Δy, Δu, Δv, Δrx, Δry, Δpx, Δpy;
	double dx, dy;

	markmunit(mu, 0);
	px = mu->px;
	py = mu->py;
	sx = mu->subpx;
	sy = mu->subpy;
	Δu = mu->vx→ * (1 << Subpxshift);
	Δv = mu->vy→ * (1 << Subpxshift);
	Δx = abs(Δu);
	Δy = abs(Δv);
	Δrx = fabs(mu->vx→ * mu->speed) * (1 << Subpxshift);
	Δry = fabs(mu->vy→ * mu->speed) * (1 << Subpxshift);
	Δpx = abs((mu->pathp->x << Subpxshift) - sx);
	Δpy = abs((mu->pathp->y << Subpxshift) - sy);
	if(Δpx < Δrx)
		Δrx = Δpx;
	if(Δpy < Δry)
		Δry = Δpy;
	while(Δrx > 0 || Δry > 0){
		x = mu->x;
		y = mu->y;
		if(Δrx > 0){
			sx += Δu;
			Δrx -= Δx;
			if(Δrx < 0)
				sx += mu->vx→ < 0 ? -Δrx : Δrx;
			x = (sx >> Subpxshift) + ((sx & Subpxmask) != 0);
			x /= Nodewidth;
		}
		if(Δry > 0){
			sy += Δv;
			Δry -= Δy;
			if(Δry < 0)
				sy += mu->vy→ < 0 ? -Δry : Δry;
			y = (sy >> Subpxshift) + ((sy & Subpxmask) != 0);
			y /= Nodewidth;
		}
		if(isblocked(x, y, mu->u))
			goto end;
		/* disallow corner coasting */
		if(x != mu->x && y != mu->y
		&& (isblocked(x, mu->y, mu->u) || isblocked(mu->x, y, mu->u))){
			dprint("detected corner coasting %d,%d vs %d,%d\n",
				x, y, mu->x, mu->y);
			goto end;
		}
		mu->subpx = sx;
		mu->subpy = sy;
		mu->px = sx >> Subpxshift;
		mu->py = sy >> Subpxshift;
		mu->x = mu->px / Nodewidth;
		mu->y = mu->py / Nodeheight;
	}
	markmunit(mu, 1);
	dx = mu->px - px;
	dx *= dx;
	dy = mu->py - py;
	dy *= dy;
	mu->pathlen -= sqrt(dx + dy) / Nodewidth;
	return 0;
end:
	werrstr("trymove: can't move to %d,%d", x, y);
	mu->subpx = mu->px << Subpxshift;
	mu->subpy = mu->py << Subpxshift;
	markmunit(mu, 1);
	dx = mu->px - px;
	dx *= dx;
	dy = mu->py - py;
	dy *= dy;
	mu->pathlen -= sqrt(dx + dy) / Nodewidth;
	return -1;
}

static int
domove(Munit *mu)
{
	int r;

	updatespeed(mu);
	unlinkmunit(mu->munitl);
	r = trymove(mu);
	linktomap(mu);
	return r;
}

static void
stepmove(Munit *mu)
{
	int n;

	n = 0;
restart:
	n++;
	if(tryturn(mu))
		return;
	if(domove(mu) < 0){
		if(n > 1){
			fprint(2, "stepmove: %s %#p bug inducing infinite loop!\n",
				mu->u->name, mu);
			return;
		}
		dprint("stepmove: failed to move: %r\n");
		if(repath(mu->target, mu) < 0){
			dprint("stepmove: %s %#p moving towards target: %r\n",
				mu->u->name, mu);
			mu->speed = 0.0;
			return;
		}
		goto restart;
	}
	if(mu->px == mu->pathp->x && mu->py == mu->pathp->y){
		mu->pathp++;
		if(mu->pathp < mu->pathe){
			nextmove(mu);
			return;
		}else if(mu->x == mu->target.x && mu->y == mu->target.y){
			mu->npatherr = 0;
			mu->speed = 0.0;
			freemove(mu);
			return;
		}
		dprint("stepmove: %s %#p reached final node, but not target\n",
			mu->u->name, mu);
		if(mu->goalblocked && isblocked(mu->target.x, mu->target.y, mu->u)){
			dprint("stepmove: %s %#p goal still blocked, stopping\n",
				mu->u->name, mu);
			mu->speed = 0.0;
			freemove(mu);
			return;
		}
		if(mu->npatherr++ > 1
		|| repath(mu->target, mu) < 0){
			dprint("stepmove: %s %#p trying to find target: %r\n",
				mu->u->name, mu);
			mu->npatherr = 0;
			mu->speed = 0.0;
			freemove(mu);
		}
	}
}

void
stepsim(void)
{
	Munitl *ml, *uml;

	for(uml=moving->l, ml=uml->l; uml!=moving; uml=ml, ml=ml->l)
		stepmove(uml->mu);
}

void
initsim(void)
{
	Team *t;

	if(nteam < 2)
		sysfatal("initgame: the only winning move is not to play");
	for(t=teams; t<=teams+nteam; t++)
		memcpy(t->r, initres, sizeof initres);
}
