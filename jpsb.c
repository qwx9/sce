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

Node **
jpsbsuccessors(Mobj *mo, Node *n, Node *goal)
{
	static Node *dir[8+1];
	static dtab[2*(nelem(dir)-1)]={
		1<<1, θN, 1<<3, θE, 1<<7, θS, 1<<5, θW,
		1<<0, θNE, 1<<6, θSE, 1<<8, θSW, 1<<2, θNW
	};
	int i, ns;
	Node *s, **p;

	ns = neighbors(n->x, n->y, mo->o->w, mo->o->h);
	ns = prune(ns, n->dir);
	memset(dir, 0, sizeof dir);
	for(i=0, p=dir; i<nelem(dtab); i+=2){
		if(ns & dtab[i])
			continue;
		if((s = jump(n->x, n->y, mo->o->w, mo->o->h, goal, dtab[i+1])) != nil){
			s->dir = dtab[i+1];
			*p++ = s;
		}
	}
	return dir;
}

/* FIXME: clean all this garbage out once map reimplemented */
static Node *nearest;
static Node **
nearestsuccessors(Mobj *mo, Node *n, Node *goal)
{
	static Node *dir[8+1];
	static dtab[2*(nelem(dir)-1)]={
		-1,-1, 0,-1, 1,-1,
		-1,0, 1,0,
		-1,1, 0,1, 1,1,
	};
	int i;
	double d;
	Node *s, **p;

	d = octdist(nearest->Point, goal->Point);
	memset(dir, 0, sizeof dir);
	for(i=0, p=dir; i<nelem(dtab); i+=2){
		s = n + dtab[i+1] * mapwidth + dtab[i];
		if(s >= map && s < map + mapwidth * mapheight){
			s->Point = addpt(n->Point, Pt(dtab[i], dtab[i+1]));
			if(octdist(s->Point, goal->Point) > d || isblocked(s->Point, mo->o))
				continue;
			s->Δg = 1;
			s->Δlen = dtab[i] != 0 && dtab[i+1] != 0 ? SQRT2 : 1;
			// UGHHHHh
			s->x = (s - map) % mapwidth;
			s->y = (s - map) / mapwidth;
			*p++ = s;
		}
	}
	return dir;
}

Node *
jpsbnearestnonjump(Mobj *mo, Node *nearestjump, Node *goal)
{
	nearest = nearestjump;
	return a∗(mo, nearestjump, goal, nearestsuccessors);
}
