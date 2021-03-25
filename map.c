#include <u.h>
#include <libc.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

Terrain **terrain;
int terwidth, terheight;
Map *map;
int mapwidth, mapheight;
Node *node;

static void
updatemap(Mobj *mo)
{
	linktomap(mo);
	markmobj(mo, 1);
}

static int
findspawn(int *nx, int *ny, int ofs, Obj *o, Obj *spawn)
{
	int sz, x, y, minx, miny, maxx, maxy;

	sz = ofs * o->w;
	minx = *nx - sz;
	miny = *ny - sz;
	maxx = *nx + spawn->w + sz;
	maxy = *ny + spawn->h + sz;
	for(x=minx+sz, y=maxy; x<maxx; x++)
		if(!isblocked(x, y, o))
			goto found;
	for(x=maxx, y=maxy; y>miny; y--)
		if(!isblocked(x, y, o))
			goto found;
	for(x=maxx, y=miny; x>minx; x--)
		if(!isblocked(x, y, o))
			goto found;
	for(x=minx, y=miny; y<=maxy; y++)
		if(!isblocked(x, y, o))
			goto found;
	return -1;
found:
	*nx = x;
	*ny = y;
	return 0;
}

static int
getspawn(int *nx, int *ny, Obj *o)
{
	int n, x, y;
	Mobj *mo;
	Map *m;

	x = *nx;
	y = *ny;
	if(o->f & Fbuild){
		if(isblocked(x, y, o)){
			werrstr("getspawn: building placement at %d,%d blocked", x, y);
			return -1;
		}
	}else if((o->f & Fair) == 0){
		m = map + y * mapwidth + x;
		if(m->ml.l == &m->ml || m->ml.l->mo == nil){
			werrstr("getspawn: no spawn object at %d,%d", x, y);
			return -1;
		}
		mo = m->ml.l->mo;
		for(n=0; n<3; n+=o->w)
			if(findspawn(&x, &y, n / o->w, o, mo->o) >= 0)
				break;
		if(n == 3){
			werrstr("getspawn: no free spot for %s at %d,%d\n",
				o->name, x, y);
			return -1;
		}
	}
	*nx = x;
	*ny = y;
	return 0;
}

Mobj *
mapspawn(int x, int y, Obj *o)
{
	Mobj *mo;

	if(o->f & Fbuild && (x & Node2Tile-1 || y & Node2Tile-1)){
		werrstr("mapspawn: building spawn %d,%d not aligned to terrain map", x, y);
		return nil;
	}
	if(getspawn(&x, &y, o) < 0)
		return nil;
	mo = emalloc(sizeof *mo);
	mo->x = x;
	mo->y = y;
	mo->px = x * Nodewidth;
	mo->py = y * Nodeheight;
	mo->subpx = mo->px << Subpxshift;
	mo->subpy = mo->py << Subpxshift;
	mo->o = o;
	mo->f = o->f;
	mo->hp = o->hp;
	mo->θ = frand() * 256;
	updatemap(mo);
	return mo;
}

void
initmap(void)
{
	int n;
	Map *m;

	mapwidth = terwidth * Node2Tile;
	mapheight = terheight * Node2Tile;
	n = mapwidth * mapheight;
	map = emalloc(n * sizeof *map);
	node = emalloc(n * sizeof *node);
	for(m=map; m<map+n; m++)
		m->ml.l = m->ml.lp = &m->ml;
	initbmap();
}
