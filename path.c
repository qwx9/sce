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

static void
clearpath(void)
{
	nukequeue(&queue);
	memset(map, 0, mapwidth * mapheight * sizeof *map);
	nearest = nil;
}

int
isblocked(Point p, Obj *o)
{
	u64int *row;

	if(o->f & Fair)
		return 0;
	row = bload(p, Pt(o->w, o->h), ZP, 0, 0);
	return (*row & 1ULL << 63) != 0;
}

Mobj *
unitat(Point p)
{
	Point mp;
	Rectangle r, mr;
	Tile *t;
	Mobjl *ml;
	Mobj *mo;

	mp = divpt(p, Node2Tile);
	r = Rpt(subpt(mp, Pt(4, 4)), mp);
	for(; mp.y>=r.min.y; mp.y--){
		mp.x = r.max.x;
		t = tilemap + mp.y * tilemapwidth + mp.x;
		for(; mp.x>=r.min.x; mp.x--, t--)
			for(ml=t->ml.l; ml!=&t->ml; ml=ml->l){
				mo = ml->mo;
				mr = Rect(mo->x, mo->y, mo->x+mo->o->w, mo->y+mo->o->h);
				if(ptinrect(p, mr))
					return mo;
			}
	}
	return nil;
}

void
markmobj(Mobj *mo, int set)
{
	Point sz;

	if(mo->o->f & Fair)
		return;
	sz = Pt(mo->o->w, mo->o->h);
/*
	if((mo->sub.x & Submask) != 0 && mo->x != ((mo->sub.x>>Pixelshift) + 1) / Nodesz)
		sz.x++;
	if((mo->sub.y & Submask) != 0 && mo->y != ((mo->sub.y>>Pixelshift) + 1) / Nodesz)
		sz.y++;
*/
	sz.x += (mo->sub.x & Submask) != 0 && mo->x != mo->sub.x + (1<<Pixelshift) >> Subshift;
	sz.y += (mo->sub.y & Submask) != 0 && mo->y != mo->sub.y + (1<<Pixelshift) >> Subshift;
	bset(mo->Point, sz, set);
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
		row = bload(Pt(x, y), Pt(w, h), Pt(0, 2), left, rot);
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
	assert(x < mapwidth && y < mapheight);
	n = map + y * mapwidth + x;
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
		if(*bload(Pt(x, y), Pt(w, h), ZP, 0, 0) & 1ULL << 63)
			return nil;
		if(jumpeast(x, y, w, h, b, &ofs1, left1, 1) != nil
		|| jumpeast(x, y, w, h, b, &ofs2, left2, 0) != nil)
			break;
		if(ofs1 == 0 || ofs2 == 0)
			return nil;
	}
	assert(x < mapwidth && y < mapheight);
	n = map + y * mapwidth + x;
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

	row = bload(Pt(x-1,y-1), Pt(w,h), Pt(2,2), 1, 0);
	return (row[2] & 7) << 6 | (row[1] & 7) << 3 | row[0] & 7;
}

static Node **
jpssuccessors(Node *n, Size sz, Node *b)
{
	static Node *dir[8+1];
	static dtab[2*(nelem(dir)-1)]={
		1<<1, θN, 1<<3, θE, 1<<7, θS, 1<<5, θW,
		1<<0, θNE, 1<<6, θSE, 1<<8, θSW, 1<<2, θNW
	};
	int i, ns;
	Node *s, **p;

	ns = neighbors(n->x, n->y, sz.w, sz.h);
	ns = prune(ns, n->dir);
	memset(dir, 0, sizeof dir);
	for(i=0, p=dir; i<nelem(dtab); i+=2){
		if(ns & dtab[i])
			continue;
		if((s = jump(n->x, n->y, sz.w, sz.h, b, dtab[i+1])) != nil){
			s->dir = dtab[i+1];
			*p++ = s;
		}
	}
	return dir;
}

static Node **
successors(Node *n, Size, Node *)
{
	static Node *dir[8+1];
	static dtab[2*(nelem(dir)-1)]={
		-1,-1, 0,-1, 1,-1,
		-1,0, 0,1,
		-1,1, 0,1, 1,1,
	};
	int i;
	Node *s, **p;

	memset(dir, 0, sizeof dir);
	for(i=0, p=dir; i<nelem(dtab); i+=2){
		s = n + dtab[i+1] * mapwidth + dtab[i];
		if(s >= map && s < map + mapwidth * mapheight){
			s->Point = addpt(n->Point, Pt(dtab[i], dtab[i+1]));
			s->Δg = 1;
			s->Δlen = dtab[i] != 0 && dtab[i+1] != 0 ? SQRT2 : 1;
			*p++ = s;
		}
	}
	return dir;
}

static Node *
a∗(Mobj *mo, Node *a, Node *b)
{
	double g, Δg;
	Node *x, *n, **dp;
	Pairheap *pn;

	if(a == b){
		werrstr("a∗: moving in place");
		return nil;
	}
	x = a;
	a->h = octdist(a->Point, b->Point);
	pushqueue(a, &queue);
	while((pn = popqueue(&queue)) != nil){
		x = pn->n;
		free(pn);
		if(x == b)
			break;
		x->closed = 1;
		dp = successors(x, mo->o->Size, b);
		for(n=*dp++; n!=nil; n=*dp++){
			if(n->closed)
				continue;
			if(isblocked(n->Point, mo->o))
				continue;
			g = x->g + n->Δg;
			Δg = n->g - g;
			if(!n->open){
				n->from = x;
				n->open = 1;
				n->step = x->step + 1;
				n->h = octdist(n->Point, b->Point);
				n->len = x->len + n->Δlen;
				n->g = g;
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
directpath(Mobj *mo, Node *a, Node *g)
{
	Path *pp;

	pp = &mo->path;
	pp->dist = eucdist(a->Point, g->Point);
	clearvec(&pp->moves, sizeof g->Point);
	pushvec(&pp->moves, &g->Point, sizeof g->Point);
	pp->step = (Point *)pp->moves.p + pp->moves.n - 1;
}

static void
backtrack(Mobj *mo, Node *n, Node *a)
{
	Path *pp;

	pp = &mo->path;
	assert(n != a && n->step > 0);
	pp->dist = n->len;
	clearvec(&pp->moves, sizeof n->Point);
	for(; n!=a; n=n->from)
		pushvec(&pp->moves, &n->Point, sizeof n->Point);
	pp->step = (Point *)pp->moves.p + pp->moves.n - 1;
}

int
isnextto(Mobj *mo, Mobj *tgt)
{
	Rectangle r1, r2;

	if(tgt == nil)
		return 0;
	r1.min = mo->Point;
	r1.max = addpt(r1.min, Pt(mo->o->w, mo->o->h));
	r2.min = tgt->Point;
	r2.max = addpt(r2.min, Pt(tgt->o->w, tgt->o->h));
	return rectXrect(insetrect(r1, -1), r2);
}

/* FIXME: completely broken */
static Node *
nearestnonjump(Mobj *mo, Node *n, Node *b)
{
	static Point dirtab[] = {
		{0,-1},
		{1,0},
		{0,1},
		{-1,0},
	};
	int i;
	Point p;
	Node *m, *min;

	min = n;
	for(i=0; i<nelem(dirtab); i++){
		p = addpt(n->Point, dirtab[i]);
		while(!isblocked(p, mo->o)){
			m = map + p.y * mapwidth + p.x;
			m->Point = p;
			m->h = octdist(m->Point, b->Point);
			if(min->h < m->h)
				break;
			min = m;
			p = addpt(p, dirtab[i]);
		}
	}
	if(min != n){
		min->from = n;
		min->open = 1;
		min->step = n->step + 1;
	}
	return min;
}

/* FIXME: completely broken */
void
setgoal(Mobj *mo, Point *gp, Mobj *block)
{
	int e;
	double Δ, Δ´;
	Point p, g;
	Node *n1, *n2, *gn;

	if(mo->o->f & Fair || block == nil){
		mo->path.blocked = 0;
		return;
	}
	g = *gp;
	mo->path.blocked = 1;
	dprint("%M setgoal: moving goal %P in block %#p ", mo, g, block);
	gn = map + g.y * mapwidth + g.x;
	gn->Point = g;
	Δ = 0x7ffffff;
	p = block->Point;
	n1 = map + p.y * mapwidth + p.x;
	n2 = n1 + (block->o->h - 1) * mapwidth;
	for(e=p.x+block->o->w; p.x<e; p.x++, n1++, n2++){
		n1->Point = p;
		Δ´ = octdist(gn->Point, n1->Point);
		if(Δ´ < Δ){
			Δ = Δ´;
			g = p;
		}
		n2->Point = addpt(p, Pt(0, block->o->h-1));
		Δ´ = octdist(gn->Point, n2->Point);
		if(Δ´ < Δ){
			Δ = Δ´;
			g = n2->Point;
		}
	}
	p = addpt(block->Point, Pt(0,1));
	n1 = map + p.y * mapwidth + p.x;
	n2 = n1 + block->o->w - 1;
	for(e=p.y+block->o->h-2; p.y<e; p.y++, n1+=mapwidth, n2+=mapwidth){
		n1->Point = p;
		Δ´ = octdist(gn->Point, n1->Point);
		if(Δ´ < Δ){
			Δ = Δ´;
			g = p;
		}
		n2->Point = addpt(p, Pt(block->o->w-1, 0));
		Δ´ = octdist(gn->Point, n2->Point);
		if(Δ´ < Δ){
			Δ = Δ´;
			g = n2->Point;
		}
	}
	dprint("to %P\n", g);
	*gp = g;
}

int
findpath(Mobj *mo, Point p)
{
	Node *a, *b, *n;

	if(eqpt(p, mo->Point)){
		werrstr("not moving to itself");
		return -1;
	}
	clearpath();
	a = map + mo->y * mapwidth + mo->x;
	a->Point = mo->Point;
	b = map + p.y * mapwidth + p.x;
	b->Point = p;
	dprint("%M findpath from %P to %P dist %f\n", mo, a->Point, b->Point, octdist(a->Point, b->Point));
	if(mo->o->f & Fair){
		directpath(mo, a, b);
		return 0;
	}
	markmobj(mo, 0);
	n = a∗(mo, a, b);
	if(n != b){
		dprint("%M findpath: goal unreachable\n", mo);
		if((n = nearest) == a || n == nil || a->h < n->h){
			werrstr("a∗: can't move");
			markmobj(mo, 1);
			return -1;
		}
		dprint("%M findpath: nearest is %#p %P dist %f\n", mo, n, n->Point, n->h);
		n = nearest;
		if(n == a){
			werrstr("a∗: really can't move");
			markmobj(mo, 1);
			return -1;
		}
		/*
		b = nearestnonjump(mo, n, b);
		if(b == a){
			werrstr("a∗: really can't move");
			markmobj(mo, 1);
			return -1;
		}
		clearpath();
		a->Point = mo->Point;
		b->Point = Pt((b - map) % mapwidth, (b - map) / mapwidth);
		if((n = a∗(mo, a, b)) != b){
			werrstr("bug: failed to find path to nearest non-jump point");
			return -1;
		}
		*/
	}
	dprint("%M found %#p at %P dist %f\n", mo, n, n->Point, n->h);
	markmobj(mo, 1);
	backtrack(mo, n, a);
	return 0;
}
