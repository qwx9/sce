#include <u.h>
#include <libc.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

Path *path;
int pathwidth, pathheight;

typedef struct Queue Queue;
struct Queue{
	Path *pp;
	Queue *q;
	Queue *p;
};
static Queue q1 = {.q = &q1, .p = &q1}, *popen = &q1;

int
isblocked(Point *p, Mobj *mo)
{
	int x, y;
	Path *pp, *e;

	if(mo->o->f & Fair)
		return 0;
	pp = path + p->y * pathwidth + p->x;
	x = mo->o->w * (Tlwidth / Tlsubwidth);
	y = mo->o->h * (Tlheight / Tlsubheight);
	while(y-- > 0){
		for(e=pp+x; pp<e; pp++)
			if(pp->blk != nil && pp->blk != mo)
				return 1;
		pp += pathwidth - x;
	}
	return 0;
}

void
setblk(Mobj *mo, int clr)
{
	int x, y;
	Path *pp, *e;
	Lobj *lo;

	pp = path + mo->y * pathwidth + mo->x;
	x = mo->o->w * (Tlwidth / Tlsubwidth);
	y = mo->o->h * (Tlheight / Tlsubheight);
	lo = mo->blk;
	while(y-- > 0){
		for(e=pp+x; pp<e; pp++)
			if(clr){
				if(pp->blk == mo)
					pp->blk = nil;
				lunlink(lo++);
			}else{
				if((mo->o->f & Fair) == 0)
					pp->blk = mo;
				llink(lo++, &pp->lo);
			}
		pp += pathwidth - x;
	}
}

static Path *
poppath(Queue *r)
{
	Path *p;
	Queue *q;

	q = r->q;
	if(q == r)
		return nil;
	p = q->pp;
	r->q = q->q;
	free(q);
	return p;
}

static void
pushpath(Queue *r, Path *pp)
{
	Queue *q, *qp, *p;

	for(qp=r, q=qp->q; q!=r && pp->g+pp->h > q->pp->g+q->pp->h; qp=q, q=q->q)
		;
	p = emalloc(sizeof *p);
	p->pp = pp;
	p->q = q;
	qp->q = p;
}

static void
clearpath(void)
{
	Queue *p, *q;
	Path *pp;

	/* FIXME: separate table(s)? */
	for(pp=path; pp<path+pathwidth*pathheight; pp++){
		pp->g = 0x7fffffff;
		pp->h = 0x7fffffff;
		pp->from = nil;
		pp->closed = 0;
		pp->open = 0;
	}
	for(p=popen->q; p!=popen; p=q){
		q = p->q;
		free(p);
	}
	popen->p = popen->q = popen;
}

static void
setpath(Path *e, Mobj *mo)
{
	int n;
	Point *p, *pe, x, p0, p1;

	/* FIXME: probably better to just replace it with a static buffer;
	 * except for static buildings and air units, units are going to move
	 * and use pathfinding almost always */
	pe = emalloc(Npath * sizeof *pe);
	mo->path = pe;
	p = pe + Npath - 1;
	n = 0;
	/* FIXME: drawing: spawn needs to center unit on path node, and
	 * drawing needs to offset sprite from path center */
	for(; e!=nil; e=e->from){
		x.x = ((e - path) % pathwidth) * Tlsubwidth + (Tlsubwidth >> 1);
		x.y = ((e - path) / pathwidth) * Tlsubheight + (Tlsubheight >> 1);
		if(n == 0){
			x.x -= Tlsubwidth >> 1;
			x.y -= Tlsubheight >> 1;
			*p-- = x;
			p0 = x;
			n++;
		}else if(n == 1){
			p1 = x;
			n++;
		}else{
			if(atan2(x.y - p0.y, x.x - p0.x)
			!= atan2(p1.y - p0.y, p1.x - p0.x)){
				if(p < pe){
					memmove(pe+1, pe, (Npath - 1) * sizeof *pe);
					*pe = p1;
				}else
					*p-- = p1;
				p0 = p1;
				n = 2;
			}
			p1 = x;
		}
	}
	mo->pathp = p + 1;
	mo->pathe = pe + Npath;
}

static double
dist(Path *p, Path *q)
{
	int dx, dy;

	dx = abs(p->x - q->x);
	dy = abs(p->y - q->y);
	return dx + dy;
	//return 1 * (dx + dy) + (1 - 2 * 1) * (dx < dy ? dx : dy);
	//return sqrt(dx * dx + dy * dy);
}

/* FIXME: air: ignore pathfinding? */
static Path **
trywalk(Path *pp, Mobj *mo)
{
	int x, y;
	Point p, *d, diff[] = {
		{1,0}, {0,-1}, {-1,0}, {0,1},
		{1,1}, {-1,1}, {-1,-1}, {1,-1},
	};
	Path **lp;
	static Path *l[nelem(diff) + 1];

	x = (pp - path) % pathwidth;
	y = (pp - path) / pathwidth;
	for(d=diff, lp=l; d<diff+nelem(diff); d++){
		p.x = x + d->x;
		p.y = y + d->y;
		if(p.x < 0 || p.x > pathwidth
		|| p.y < 0 || p.y > pathheight)
			continue;
		if(!isblocked(&p, mo))
			*lp++ = path + p.y * pathwidth + p.x;
	}
	*lp = nil;
	return l;
}

static int
a∗(Mobj *mo, Point *p2)
{
	double g;
	Path *s, *pp, *e, *q, **l;

	clearpath();
	s = path + pathwidth * mo->y + mo->x;
	e = path + pathwidth * (p2->y / Tlsubheight) + p2->x / Tlsubwidth;
	/* FIXME: don't return an error, just an empty path, or the same
	 * block as the source */
	if(s == e){
		werrstr("no.");
		return -1;
	}
	s->g = 0;
	s->h = dist(s, e);
	pushpath(popen, s);
	while((pp = poppath(popen)) != nil){
		if(pp == e)
			break;
		pp->closed = 1;
		pp->open = 0;
		l = trywalk(pp, mo);
		for(q=*l; q!=nil; q=*l++)
			if(!q->closed){
				g = pp->g + dist(pp, q);
				if(!q->open){
					q->from = pp;
					q->g = g;
					q->h = dist(q, e);
					q->open = 1;
					pushpath(popen, q);
				}else if(g < q->g){
					q->from = pp;
					q->g = g;
				}
			}
	}
	/* FIXME: move towards target anyway */
	if(e->from == nil && pp != e){
		werrstr("NOPE");
		return -1;
	}
	setpath(e, mo);
	return 0;
}

int
findpath(Mobj *mo, Point *p2)
{
	return a∗(mo, p2);
}

void
initpath(void)
{
	int n, x, y;
	Path *pp;

	pathwidth = mapwidth * (Tlwidth / Tlsubwidth);
	pathheight = mapheight * (Tlheight / Tlsubheight);
	n = pathwidth * pathheight;
	path = emalloc(n * sizeof *path);
	for(y=0, pp=path; y<pathheight; y++)
		for(x=0; x<pathwidth; x++, pp++){
			pp->x = x;
			pp->y = y;
			pp->lo.lo = pp->lo.lp = &pp->lo;
		}
}
