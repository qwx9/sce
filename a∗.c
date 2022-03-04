#include <u.h>
#include <libc.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

static Pairheap *queue;
static Node *nearest;

static void
clearpath(void)
{
	nukequeue(&queue);
	memset(map, 0, mapwidth * mapheight * sizeof *map);
	nearest = nil;
}

static Node *
a∗nearestfree(Node*, Node*, Node *nearest)
{
	return nearest;
}

void
a∗backtrack(Mobj *mo, Node *b, Node *a)
{
	Path *pp;

	pp = &mo->path;
	assert(b != a && b->step > 0);
	pp->dist = b->len;
	clearvec(&pp->moves);
	for(; b!=a; b=b->from){
		assert(b != nil);
		dprint("%M a∗: backtracking: %#p %P dist %f from %#p\n",
			mo, b, b->Point, b->h, b->from);
		pushvec(&pp->moves, &b->Point);
	}
	pp->step = (Point *)pp->moves.p + pp->moves.n - 1;
}

static Node **
a∗successors(Mobj *mo, Node *n, Node*)
{
	static Node *dir[8+1];
	static dtab[2*(nelem(dir)-1)]={
		-1,-1, 0,-1, 1,-1,
		-1,0, 1,0,
		-1,1, 0,1, 1,1,
	};
	int i;
	Node *s, **p;

	memset(dir, 0, sizeof dir);
	for(i=0, p=dir; i<nelem(dtab); i+=2){
		s = n + dtab[i+1] * mapwidth + dtab[i];
		if(s >= map && s < map + mapwidth * mapheight){
			s->Point = addpt(n->Point, Pt(dtab[i], dtab[i+1]));
			if(isblocked(s->Point, mo->o))
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
a∗(Mobj *mo, Node *a, Node *b, Node**(*successorsfn)(Mobj*,Node*,Node*))
{
	double g, Δg;
	Node *x, *n, **dp;
	Pairheap *pn;

	if(a == b){
		werrstr("a∗: moving in place");
		return nil;
	}
	clearpath();
	a->Point = mo->Point;
	b->Point = Pt((b-map)%mapwidth, (b-map)/mapwidth);
	x = a;
	a->h = octdist(a->Point, b->Point);
	pushqueue(a, &queue);
	while((pn = popqueue(&queue)) != nil){
		x = pn->n;
		free(pn);
		if(x == b)
			break;
		x->closed = 1;
		dp = successorsfn(mo, x, b);
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
				assert(n->g >= 0);
			}
			if(nearest == nil || n->h < nearest->h){
				nearest = n;
				dprint("%M a∗: nearest node now %#p %P dist %f\n",
					mo, n, n->Point, n->h);
			}
		}
	}
	return x;
}

Node *
a∗findpath(Mobj *mo, Node *a, Node *b)
{
	Node *n, *m;

	if(a∗(mo, a, b, jpsbsuccessors) == b){
		dprint("%M a∗path: successfully found %#p at %P dist %f\n", mo, b, b->Point, b->h);
		return b;
	}
	dprint("%M a∗findpath: goal unreachable\n", mo);
	n = nearest;
	if(n == a || n == nil){
		werrstr("a∗findpath: can't move");
		return nil;
	}
	dprint("%M findpath: nearest jump is %#p %P dist %f\n", mo, n, n->Point, n->h);
	m = jpsbnearestnonjump(mo, n, b);
	if(nearest == nil || nearest == n){
		dprint("%M a∗findpath: failed to find nearer non-jump point\n", mo);
		nearest = n;
	}
	if(m == b){
		dprint("%M a∗findpath: jps pathfinding failed but plain a∗ found the goal\n", mo);
		nearest = b;
	}
	/* m( */
	m = a∗(mo, a, nearest, a∗successors);
	return m == b ? b : nearest;
}
