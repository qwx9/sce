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
updatemap(Mobj *mo)
{
	Mobj *bmo;

	if(isblocked(mo->x, mo->y, mo->o)){
		bmo = unitat(mo->x, mo->y);
		sysfatal("markmobj: attempt to place %s at %d,%d, non-free block having %s at %d,%d",
			mo->o->name, mo->x, mo->y, bmo->o->name, bmo->x, bmo->y);
	}
	linktomap(mo);
	markmobj(mo, 1);
}

static int
findspawn(int *nx, int *ny, int ofs, Obj *o, Obj *spawn)
{
	int x, y, minx, miny, maxx, maxy;

	minx = *nx - (ofs+1) * o->w;
	miny = *ny - (ofs+1) * o->h;
	maxx = *nx + spawn->w + ofs * o->w;
	maxy = *ny + spawn->h + ofs * o->h;
	for(x=minx+o->w, y=maxy; x<maxx; x++)
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
	Map *m;
	Mobjl *ml;
	Mobj *mo;
	Obj **os;

	x = *nx;
	y = *ny;
	if(o->f & Fbuild){
		if(isblocked(x, y, o)){
			werrstr("getspawn: building placement at %d,%d blocked", x, y);
			return -1;
		}
	}else if((o->f & Fair) == 0){
		m = map + y / Node2Tile * mapwidth + x / Node2Tile;
		for(mo=nil, ml=m->ml.l; ml!=&m->ml; ml=ml->l){
			mo = ml->mo;
			for(os=mo->o->spawn, n=mo->o->nspawn; n>0; n--, os++)
				if(*os == o)
					break;
			if(n > 0)
				break;
		}
		if(ml == &m->ml){
			werrstr("getspawn: no spawn object at %d,%d", x, y);
			return -1;
		}
		for(n=0; n<3; n++)
			if(findspawn(&x, &y, n, o, mo->o) >= 0)
				break;
		if(n == 3){
			werrstr("getspawn: no free spot for %s at %d,%d",
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
		werrstr("mapspawn: building spawn %d,%d not aligned to tile map", x, y);
		return nil;
	}
	if(getspawn(&x, &y, o) < 0)
		return nil;
	mo = emalloc(sizeof *mo);
	mo->uuid = lrand();
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
	nodemapwidth = mapwidth * Node2Tile;
	nodemapheight = mapheight * Node2Tile;
	nodemap = emalloc(nodemapwidth * nodemapheight * sizeof *nodemap);
	initbmap();
}
