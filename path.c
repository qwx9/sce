#include <u.h>
#include <libc.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

void
drawnodemap(Rectangle r, Mobj *sel)
{
	u64int *row, v, m;
	Point p, *pp;
	Path *path;
	Node *n;

	r = Rpt(mulpt(r.min, Node2Tile), mulpt(r.max, Node2Tile));
	for(p.y=r.min.y, n=map+p.y*mapwidth+r.min.x; p.y<r.max.y; p.y++){
		p.x = r.min.x;
		row = baddr(p);
		v = *row++;
		m = 1ULL << 63 - (p.x & Bmask);
		for(; p.x<r.max.x; p.x++, n++, m>>=1){
			if(m == 0){
				v = *row++;
				m = 1ULL << 63;
			}
			if(v & m)
				compose(mulpt(p, Nodesz), 0xff0000);
			if(n->closed)
				compose(mulpt(p, Nodesz), 0x000077);
			else if(n->open)
				compose(mulpt(p, Nodesz), 0x007777);
		}
		n += mapwidth - (r.max.x - r.min.x);
	}
	if(sel != nil){
		path = &sel->path;
		if(path->step == nil)
			return;
		for(pp=path->step; pp>=path->moves.p; pp--)
			compose(mulpt(*pp, Nodesz), 0x00ff00);
		compose(mulpt(path->target, Nodesz), 0x00ff77);
	}
}

double
eucdist(Point a, Point b)
{
	int dx, dy;

	dx = a.x - b.x;
	dy = a.y - b.y;
	return sqrt(dx * dx + dy * dy);
}

double
octdist(Point a, Point b)
{
	int dx, dy;

	dx = abs(a.x - b.x);
	dy = abs(a.y - b.y);
	return 1 * (dx + dy) + min(dx, dy) * (SQRT2 - 2 * 1);
}


static void
directpath(Mobj *mo, Node *a, Node *g)
{
	Path *pp;

	pp = &mo->path;
	pp->dist = eucdist(a->Point, g->Point);
	clearvec(&pp->moves);
	pushvec(&pp->moves, &g->Point);
	pp->step = (Point *)pp->moves.p + pp->moves.n - 1;
}

void
setgoal(Mobj *mo, Point *gp, Mobj *block)
{
	Point p;

	if(mo->o->f & Fair || block == nil)
		mo->path.blocked = 0;
	if(block == nil)
		return;
	p = addpt(block->Point, divpt(block->o->Size, 2));
	dprint("%M setgoal: moving goal from %P to %P\n", mo, *gp, p);
	*gp = p;
}

/* FIXME: fmt for Nodes or w/e */
int
findpath(Mobj *mo, Point p)
{
	Node *a, *b, *n;

	if(eqpt(p, mo->Point)){
		werrstr("not moving to itself");
		return -1;
	}
	a = map + mo->y * mapwidth + mo->x;
	a->Point = mo->Point;
	b = map + p.y * mapwidth + p.x;
	b->Point = p;
	dprint("%M findpath from %P to %P dist %f\n",
		mo, a->Point, b->Point, octdist(a->Point, b->Point));
	if(mo->o->f & Fair){
		directpath(mo, a, b);
		return 0;
	}
	markmobj(mo, 0);
	n = a∗findpath(mo, a, b);
	markmobj(mo, 1);
	if(n == nil){
		dprint("%M findpath: failed\n", mo);
		return -1;
	}
	dprint("%M findpath: setting path to goal %P dist %f\n", mo, n->Point, n->h);
	a∗backtrack(mo, n, a);
	return 0;
}
