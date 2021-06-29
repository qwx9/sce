#include <u.h>
#include <libc.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

/* jump point search with block-based symmetry breaking (JPS(B): 2014, harabor and
 * grastien), using pairing heaps for priority queues and a bitmap representing the
 * entire map.
 * no preprocessing since we'd have to repair the database each time anything moves,
 * which is a pain.
 * no pruning of intermediate nodes (JPS(B+P)) as of yet, until other options are
 * assessed.
 * the pruning rules adhere to (2012, harabor and grastien) to disallow corner cutting
 * in diagonal movement, and movement code elsewhere reflects that.
 * if there is no path to the target, the unit still has to move to the nearest
 * accessible node.  if there is such a node, we first attempt to find a nearer
 * non-jump point in a cardinal direction, and if successful, the point is added at
 * the end of the path.  unlike plain a∗, we cannot rely on the path backtracked from
 * the nearest node, since it is no longer guaranteed to be optimal, and will in fact
 * go all over the place.  unless jump points can be connected to all other visible
 * jump points so as to perform a search on this reduced graph without rediscovering
 * the map, we're forced to re-do pathfinding to this nearest node.  the search should
 * be much quicker since this new node is accessible.
 * pathfinding is not limited to an area, so entire map may be scanned, which is too
 * slow.  simple approaches don't seem to work well, it would perhaps be better to
 * only consider a sub-grid of the map, but the data structures currently used do not
 * allow it.  since the pathfinding algorithm will probably change, the current
 * implementation disregards the issue.
 * pathfinding is limited by number of moves (the cost function).  this prevents the
 * search to look at the entire map, but also means potentially non-optimal paths and
 * more pathfinding when crossing the boundaries.
 * since units are bigger than the pathfinding grid, the grid is "compressed" when
 * scanned by using a sliding window the size of the unit, so the rest of the algorithm
 * still operates on 3x3 neighbor grids, with each bit checking as many nodes as needed
 * for impassibility.  such an approach has apparently not been discussed in regards
 * to JPS(B), possibly since JPS(B) is a particular optimization of the original
 * algorithm and this snag may rarely be hit in practice.
 * map dimensions are assumed to be multiples of 16 tiles.
 * the code is currently horrendously ugly, though short, and ultimately wrong.
 * movement should occur at any angle (rather than in 8 directions) and unit sizes
 * do not have a common denominator higher than 1 pixel. */

enum{
	θ∅ = 0,
	θN,
	θE,
	θS,
	θW,
	θNE,
	θSE,
	θSW,
	θNW,
};

#define SQRT2 1.4142135623730951

static Pairheap *queue;
static Node *nearest;

static void
clearpath(void)
{
	nukequeue(&queue);
	memset(nodemap, 0, nodemapwidth * nodemapheight * sizeof *nodemap);
	nearest = nil;
}

int
isblocked(int x, int y, Unit *u)
{
	u64int *row;

	if(u->f & Fair)
		return 0;
	row = bload(x, y, u->w, u->h, 0, 0, 0, 0);
	return (*row & 1ULL << 63) != 0;
}

Munit *
unitat(int px, int py)
{
	int x, y;
	Rectangle r, mr;
	Map *m;
	Munitl *ml;
	Munit *mu;

	x = px / Node2Tile;
	y = py / Node2Tile;
	r = Rect(x-4, y-4, x, y);
	for(; y>=r.min.y; y--)
		for(x=r.max.x, m=map+y*mapwidth+x; x>=r.min.x; x--)
			for(ml=m->ml.l; ml!=&m->ml; ml=ml->l){
				mu = ml->mu;
				mr.min.x = mu->x;
				mr.min.y = mu->y;
				mr.max.x = mr.min.x + mu->u->w;
				mr.max.y = mr.min.y + mu->u->h;
				if(px >= mu->x && px <= mu->x + mu->u->w
				&& py >= mu->y && py <= mu->y + mu->u->h)
					return mu;
			}
	return nil;
}

void
markmunit(Munit *mu, int set)
{
	int w, h;

	if(mu->u->f & Fair)
		return;
	w = mu->u->w;
	if((mu->subpx & Subpxmask) != 0 && mu->x != (mu->px + 1) / Nodewidth)
		w++;
	h = mu->u->h;
	if((mu->subpy & Subpxmask) != 0 && mu->y != (mu->py + 1) / Nodewidth)
		h++;
	bset(mu->x, mu->y, w, h, set);
}

static double
eucdist(Node *a, Node *b)
{
	double dx, dy;

	dx = a->x - b->x;
	dy = a->y - b->y;
	return sqrt(dx * dx + dy * dy);
}

static double
octdist(Node *a, Node *b)
{
	int dx, dy;

	dx = abs(a->x - b->x);
	dy = abs(a->y - b->y);
	return 1 * (dx + dy) + (SQRT2 - 2 * 1) * min(dx, dy);
}

/* FIXME: horrendous. use fucking tables you moron */
static Node *
jumpeast(int x, int y, int w, int h, Node *b, int *ofs, int left, int rot)
{
	int nbits, steps, stop, end, *u, *v, ss, Δu, Δug, Δug2, Δvg;
	u64int bs, *row;
	Node *n;

	if(rot){
		u = &y;
		v = &x;
		Δug = b->y - y;
		Δvg = b->x - x;
	}else{
		u = &x;
		v = &y;
		Δug = b->x - x;
		Δvg = b->y - y;
	}
	steps = 0;
	nbits = 64 - w + 1;
	ss = left ? -1 : 1;
	(*v)--;
	for(;;){
		row = bload(x, y, w, h, 0, 2, left, rot);
		bs = row[1];
		if(left){
			bs |= row[0] << 1 & ~row[0];
			bs |= row[2] << 1 & ~row[2];
		}else{
			bs |= row[0] >> 1 & ~row[0];
			bs |= row[2] >> 1 & ~row[2];
		}
		if(bs)
			break;
		(*u) += ss * nbits;
		steps += nbits;
	}
	if(left){
		stop = lsb(bs);
		Δu = stop;
	}else{
		stop = msb(bs);
		Δu = 63 - stop;
	}
	end = (row[1] & 1ULL << stop) != 0;
	(*u) += ss * Δu;
	(*v)++;
	steps += Δu;
	Δug2 = rot ? b->y - y : b->x - x;
	if(ofs != nil)
		*ofs = steps;
	if(end && Δug2 == 0)
		return nil;
	if(Δvg == 0 && (Δug == 0 || (Δug < 0) ^ (Δug2 < 0))){
		b->Δg = steps - abs(Δug2);
		b->Δlen = b->Δg;
		return b;
	}
	if(end)
		return nil;
	assert(x < nodemapwidth && y < nodemapheight);
	n = nodemap + y * nodemapwidth + x;
	n->x = x;
	n->y = y;
	n->Δg = steps;
	n->Δlen = steps;
	return n;
}

static Node *
jumpdiag(int x, int y, int w, int h, Node *b, int dir)
{
	int left1, ofs1, left2, ofs2, Δx, Δy, steps;
	Node *n;

	steps = 0;
	left1 = left2 = Δx = Δy = 0;
	switch(dir){
	case θNE: left1 = 1; left2 = 0; Δx = 1; Δy = -1; break;
	case θSW: left1 = 0; left2 = 1; Δx = -1; Δy = 1; break;
	case θNW: left1 = 1; left2 = 1; Δx = -1; Δy = -1; break;
	case θSE: left1 = 0; left2 = 0; Δx = 1; Δy = 1; break;
	}
	for(;;){
		steps++;
		x += Δx;
		y += Δy;
		if(*bload(x, y, w, h, 0, 0, 0, 0) & 1ULL << 63)
			return nil;
		if(jumpeast(x, y, w, h, b, &ofs1, left1, 1) != nil
		|| jumpeast(x, y, w, h, b, &ofs2, left2, 0) != nil)
			break;
		if(ofs1 == 0 || ofs2 == 0)
			return nil;
	}
	assert(x < nodemapwidth && y < nodemapheight);
	n = nodemap + y * nodemapwidth + x;
	n->x = x;
	n->y = y;
	n->Δg = steps;
	n->Δlen = steps * SQRT2;
	return n;
}

static Node *
jump(int x, int y, int w, int h, Node *b, int dir)
{
	Node *n;

	switch(dir){
	case θE: n = jumpeast(x, y, w, h, b, nil, 0, 0); break;
	case θW: n = jumpeast(x, y, w, h, b, nil, 1, 0); break;
	case θS: n = jumpeast(x, y, w, h, b, nil, 0, 1); break;
	case θN: n = jumpeast(x, y, w, h, b, nil, 1, 1); break;
	default: n = jumpdiag(x, y, w, h, b, dir); break;
	}
	return n;
}

/* 2012, harabor and grastien: disabling corner cutting implies that only moves in
 * a cardinal direction may produce forced neighbors */
static int
forced(int n, int dir)
{
	int m;

	m = 0;
	switch(dir){
	case θN:
		if((n & (1<<8 | 1<<5)) == 1<<8) m |= 1<<5 | 1<<2;
		if((n & (1<<6 | 1<<3)) == 1<<6) m |= 1<<3 | 1<<0;
		break;
	case θE:
		if((n & (1<<2 | 1<<1)) == 1<<2) m |= 1<<1 | 1<<0;
		if((n & (1<<8 | 1<<7)) == 1<<8) m |= 1<<7 | 1<<6;
		break;
	case θS:
		if((n & (1<<2 | 1<<5)) == 1<<2) m |= 1<<5 | 1<<8;
		if((n & (1<<0 | 1<<3)) == 1<<0) m |= 1<<3 | 1<<6;
		break;
	case θW:
		if((n & (1<<0 | 1<<1)) == 1<<0) m |= 1<<1 | 1<<2;
		if((n & (1<<6 | 1<<7)) == 1<<6) m |= 1<<7 | 1<<8;
		break;
	}
	return m;
}

static int
natural(int n, int dir)
{
	int m;

	switch(dir){
	/* disallow corner coasting on the very first move */
	default:
		if((n & (1<<1 | 1<<3)) != 0)
			n |= 1<<0;
		if((n & (1<<7 | 1<<3)) != 0)
			n |= 1<<6;
		if((n & (1<<7 | 1<<5)) != 0)
			n |= 1<<8;
		if((n & (1<<1 | 1<<5)) != 0)
			n |= 1<<2;
		return n;
	case θN: return n | ~(1<<1);
	case θE: return n | ~(1<<3);
	case θS: return n | ~(1<<7);
	case θW: return n | ~(1<<5);
	case θNE: m = 1<<1 | 1<<3; return (n & m) == 0 ? n | ~(1<<0 | m) : n | 1<<0;
	case θSE: m = 1<<7 | 1<<3; return (n & m) == 0 ? n | ~(1<<6 | m) : n | 1<<6;
	case θSW: m = 1<<7 | 1<<5; return (n & m) == 0 ? n | ~(1<<8 | m) : n | 1<<8;
	case θNW: m = 1<<1 | 1<<5; return (n & m) == 0 ? n | ~(1<<2 | m) : n | 1<<2;
	}
}

static int
prune(int n, int dir)
{
	return natural(n, dir) & ~forced(n, dir);
}

static int
neighbors(int x, int y, int w, int h)
{
	u64int *row;

	row = bload(x-1, y-1, w, h, 2, 2, 1, 0);
	return (row[2] & 7) << 6 | (row[1] & 7) << 3 | row[0] & 7;
}

static Node **
successors(Node *n, int w, int h, Node *b)
{
	static Node *dir[8+1];
	static dtab[2*(nelem(dir)-1)]={
		1<<1, θN, 1<<3, θE, 1<<7, θS, 1<<5, θW,
		1<<0, θNE, 1<<6, θSE, 1<<8, θSW, 1<<2, θNW
	};
	int i, ns;
	Node *s, **p;

	ns = neighbors(n->x, n->y, w, h);
	ns = prune(ns, n->dir);
	memset(dir, 0, sizeof dir);
	for(i=0, p=dir; i<nelem(dtab); i+=2){
		if(ns & dtab[i])
			continue;
		if((s = jump(n->x, n->y, w, h, b, dtab[i+1])) != nil){
			s->dir = dtab[i+1];
			*p++ = s;
		}
	}
	return dir;
}

static Node *
a∗(Node *a, Node *b, Munit *mu)
{
	double g, Δg;
	Node *x, *n, **dp;
	Pairheap *pn;

	if(a == b){
		werrstr("a∗: moving in place");
		return nil;
	}
	x = a;
	a->h = octdist(a, b);
	pushqueue(a, &queue);
	while((pn = popqueue(&queue)) != nil){
		x = pn->n;
		free(pn);
		if(x == b)
			break;
		x->closed = 1;
		dp = successors(x, mu->u->w, mu->u->h, b);
		for(n=*dp++; n!=nil; n=*dp++){
			if(n->closed)
				continue;
			g = x->g + n->Δg;
			Δg = n->g - g;
			if(!n->open){
				n->from = x;
				n->g = g;
				n->h = octdist(n, b);
				n->len = x->len + n->Δlen;
				n->open = 1;
				n->step = x->step + 1;
				pushqueue(n, &queue);
			}else if(Δg > 0){
				n->from = x;
				n->step = x->step + 1;
				n->len = x->len + n->Δlen;
				n->g -= Δg;
				decreasekey(n->p, Δg, &queue);
			}
			if(nearest == nil || n->h < nearest->h)
				nearest = n;
		}
	}
	return x;
}

static void
resizepathbuf(Munit *mu, int nstep)
{
	if(mu->npathbuf >= nstep)
		return;
	nstep = nstep + 16;
	mu->paths = erealloc(mu->paths, nstep * sizeof mu->paths, mu->npathbuf * sizeof mu->paths);
	mu->npathbuf = nstep;
}

static void
directpath(Node *a, Node *g, Munit *mu)
{
	resizepathbuf(mu, 1);
	mu->pathlen = eucdist(a, g);
	mu->pathe = mu->paths + 1;
	mu->paths->x = g->x * Nodewidth;
	mu->paths->y = g->y * Nodewidth;
}

static void
backtrack(Node *n, Node *a, Munit *mu)
{
	int x, y;
	Point *p;

	assert(n != a && n->step > 0);
	resizepathbuf(mu, n->step);
	mu->pathlen = n->len;
	p = mu->paths + n->step;
	mu->pathe = p--;
	for(; n!=a; n=n->from){
		x = n->x * Nodewidth;
		y = n->y * Nodeheight;
		*p-- = (Point){x, y};
	}
	assert(p == mu->paths - 1);
}

static Node *
nearestnonjump(Node *n, Node *b, Munit *mu)
{
	static Point dirtab[] = {
		{0,-1},
		{1,0},
		{0,1},
		{-1,0},
	};
	int i, x, y;
	Node *m, *min;

	min = n;
	for(i=0; i<nelem(dirtab); i++){
		x = n->x + dirtab[i].x;
		y = n->y + dirtab[i].y;
		while(!isblocked(x, y, mu->u)){
			m = nodemap + y * nodemapwidth + x;
			m->x = x;
			m->y = y;
			m->h = octdist(m, b);
			if(min->h < m->h)
				break;
			min = m;
			x += dirtab[i].x;
			y += dirtab[i].y;
		}
	}
	if(min != n){
		min->from = n;
		min->open = 1;
		min->step = n->step + 1;
	}
	return min;
}

void
setgoal(Point *p, Munit *mu, Munit *block)
{
	int x, y, e;
	double Δ, Δ´;
	Node *n1, *n2, *pm;

	if(mu->u->f & Fair || block == nil){
		mu->goalblocked = 0;
		return;
	}
	mu->goalblocked = 1;
	dprint("setgoal: moving goal %d,%d in block %#p ", p->x, p->y, block);
	pm = nodemap + p->y * nodemapwidth + p->x;
	pm->x = p->x;
	pm->y = p->y;
	Δ = 0x7ffffff;
	x = block->x;
	y = block->y;
	n1 = nodemap + y * nodemapwidth + x;
	n2 = n1 + (block->u->h - 1) * nodemapwidth;
	for(e=x+block->u->w; x<e; x++, n1++, n2++){
		n1->x = x;
		n1->y = y;
		Δ´ = octdist(pm, n1);
		if(Δ´ < Δ){
			Δ = Δ´;
			p->x = x;
			p->y = y;
		}
		n2->x = x;
		n2->y = y + block->u->h - 1;
		Δ´ = octdist(pm, n2);
		if(Δ´ < Δ){
			Δ = Δ´;
			p->x = x;
			p->y = y + block->u->h - 1;
		}
	}
	x = block->x;
	y = block->y + 1;
	n1 = nodemap + y * nodemapwidth + x;
	n2 = n1 + block->u->w - 1;
	for(e=y+block->u->h-2; y<e; y++, n1+=nodemapwidth, n2+=nodemapwidth){
		n1->x = x;
		n1->y = y;
		Δ´ = octdist(pm, n1);
		if(Δ´ < Δ){
			Δ = Δ´;
			p->x = x;
			p->y = y;
		}
		n2->x = x + block->u->w - 1;
		n2->y = y;
		Δ´ = octdist(pm, n2);
		if(Δ´ < Δ){
			Δ = Δ´;
			p->x = x + block->u->w - 1;
			p->y = y;
		}
	}
	dprint("to %d,%d\n", p->x, p->y);
}

int
findpath(Point p, Munit *mu)
{
	Node *a, *b, *n;

	dprint("findpath %d,%d → %d,%d\n", mu->x, mu->y, p.x, p.y);
	clearpath();
	a = nodemap + mu->y * nodemapwidth + mu->x;
	a->x = mu->x;
	a->y = mu->y;
	b = nodemap + p.y * nodemapwidth + p.x;
	b->x = p.x;
	b->y = p.y;
	if(mu->u->f & Fair){
		directpath(a, b, mu);
		return 0;
	}
	markmunit(mu, 0);
	n = a∗(a, b, mu);
	if(n != b){
		dprint("findpath: goal unreachable\n");
		if((n = nearest) == a || n == nil || a->h < n->h){
			werrstr("a∗: can't move");
			markmunit(mu, 1);
			return -1;
		}
		dprint("nearest: %#p %d,%d dist %f\n", n, n->x, n->y, n->h);
		b = nearestnonjump(n, b, mu);
		if(b == a){
			werrstr("a∗: really can't move");
			markmunit(mu, 1);
			return -1;
		}
		clearpath();
		a->x = mu->x;
		a->y = mu->y;
		b->x = (b - nodemap) % nodemapwidth;
		b->y = (b - nodemap) / nodemapwidth;
		if((n = a∗(a, b, mu)) == nil)
			sysfatal("findpath: phase error");
	}
	markmunit(mu, 1);
	backtrack(n, a, mu);
	return 0;
}
