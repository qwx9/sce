#include <u.h>
#include <libc.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

Map *map;
int mapwidth, mapheight;
Node *nodemap;
int nodemapwidth, nodemapheight;

static void
updatemap(Munit *mu)
{
	Munit *bmu;

	if(isblocked(mu->x, mu->y, mu->u)){
		bmu = unitat(mu->x, mu->y);
		sysfatal("markmunit: attempt to place %s at %d,%d, non-free block having %s at %d,%d",
			mu->u->name, mu->x, mu->y, bmu->u->name, bmu->x, bmu->y);
	}
	linktomap(mu);
	markmunit(mu, 1);
}

static int
findspawn(int *nx, int *ny, int ofs, Unit *u, Unit *spawn)
{
	int x, y, minx, miny, maxx, maxy;

	minx = *nx - (ofs+1) * u->w;
	miny = *ny - (ofs+1) * u->h;
	maxx = *nx + spawn->w + ofs * u->w;
	maxy = *ny + spawn->h + ofs * u->h;
	for(x=minx+u->w, y=maxy; x<maxx; x++)
		if(!isblocked(x, y, u))
			goto found;
	for(x=maxx, y=maxy; y>miny; y--)
		if(!isblocked(x, y, u))
			goto found;
	for(x=maxx, y=miny; x>minx; x--)
		if(!isblocked(x, y, u))
			goto found;
	for(x=minx, y=miny; y<=maxy; y++)
		if(!isblocked(x, y, u))
			goto found;
	return -1;
found:
	*nx = x;
	*ny = y;
	return 0;
}

static int
getspawn(int *nx, int *ny, Unit *u)
{
	int n, x, y;
	Map *m;
	Munitl *ml;
	Munit *mu;
	Unit **us;

	x = *nx;
	y = *ny;
	if(u->f & Fbuild){
		if(isblocked(x, y, u)){
			werrstr("getspawn: building placement at %d,%d blocked", x, y);
			return -1;
		}
	}else{
		m = map + y / Node2Tile * mapwidth + x / Node2Tile;
		for(mu=nil, ml=m->ml.l; ml!=&m->ml; ml=ml->l){
			mu = ml->mu;
			for(us=mu->u->spawn, n=mu->u->nspawn; n>0; n--, us++)
				if(*us == u)
					break;
			if(n > 0)
				break;
		}
		if(ml == &m->ml){
			werrstr("getspawn: no spawn unit at %d,%d", x, y);
			return -1;
		}
		for(n=0; n<3; n++)
			if(findspawn(&x, &y, n, u, mu->u) >= 0)
				break;
		if(n == 3){
			werrstr("getspawn: no free spot for %s at %d,%d",
				u->name, x, y);
			return -1;
		}
	}
	*nx = x;
	*ny = y;
	return 0;
}

Munit *
mapspawn(int x, int y, Unit *u)
{
	Munit *mu;

	if(u->f & Fbuild && (x & Node2Tile-1 || y & Node2Tile-1)){
		werrstr("mapspawn: building spawn %d,%d not aligned to tile map", x, y);
		return nil;
	}
	if(getspawn(&x, &y, u) < 0)
		return nil;
	mu = emalloc(sizeof *mu);
	mu->uuid = lrand();
	mu->x = x;
	mu->y = y;
	mu->px = x * Nodewidth;
	mu->py = y * Nodeheight;
	mu->subpx = mu->px << Subpxshift;
	mu->subpy = mu->py << Subpxshift;
	mu->u = u;
	mu->f = u->f;
	mu->hp = u->hp;
	mu->θ = frand() * 256;
	updatemap(mu);
	return mu;
}

void
initmap(void)
{
	nodemapwidth = mapwidth * Node2Tile;
	nodemapheight = mapheight * Node2Tile;
	nodemap = emalloc(nodemapwidth * nodemapheight * sizeof *nodemap);
	initbmap();
}
