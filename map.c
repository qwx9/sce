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

	if(isblocked(mo->Point, mo->o)){
		bmo = unitat(mo->x, mo->y);
		sysfatal("markmobj: attempt to place %s at %P, non-free block having %s at %P",
			mo->o->name, mo->Point, bmo->o->name, bmo->Point);
	}
	linktomap(mo);
	markmobj(mo, 1);
}

static int
findspawn(Point *pp, int ofs, Obj *o, Obj *spawn)
{
	int minx, miny, maxx, maxy;
	Point p;

	p = *pp;
	minx = p.x - (ofs+1) * o->w;
	miny = p.y - (ofs+1) * o->h;
	maxx = p.x + spawn->w + ofs * o->w;
	maxy = p.y + spawn->h + ofs * o->h;
	for(p.x=minx+o->w, p.y=maxy; p.x<maxx; p.x++)
		if(!isblocked(p, o))
			goto found;
	for(p.x=maxx, p.y=maxy; p.y>miny; p.y--)
		if(!isblocked(p, o))
			goto found;
	for(p.x=maxx, p.y=miny; p.x>minx; p.x--)
		if(!isblocked(p, o))
			goto found;
	for(p.x=minx, p.y=miny; p.y<=maxy; p.y++)
		if(!isblocked(p, o))
			goto found;
	return -1;
found:
	*pp = p;
	return 0;
}

static int
getspawn(Point *pp, Obj *o)
{
	int n;
	Point p;
	Map *m;
	Mobjl *ml;
	Mobj *mo;
	Obj **os;

	p = *pp;
	if(o->f & (Fbuild|Fimmutable)){
		if(isblocked(p, o)){
			werrstr("getspawn: building placement at %P blocked", p);
			return -1;
		}
	}else{
		m = map + p.y / Node2Tile * mapwidth + p.x / Node2Tile;
		for(mo=nil, ml=m->ml.l; ml!=&m->ml; ml=ml->l){
			mo = ml->mo;
			for(os=mo->o->spawn, n=mo->o->nspawn; n>0; n--, os++)
				if(*os == o)
					break;
			if(n > 0)
				break;
		}
		if(ml == &m->ml){
			werrstr("getspawn: no spawn object at %P", p);
			return -1;
		}
		for(n=0; n<3; n++)
			if(findspawn(&p, n, o, mo->o) >= 0)
				break;
		if(n == 3){
			werrstr("getspawn: no free spot for %s at %P",
				o->name, p);
			return -1;
		}
	}
	*pp = p;
	return 0;
}

Mobj *
mapspawn(Point p, Obj *o)
{
	Mobj *mo;

	if(o->f & (Fbuild|Fimmutable) && (p.x & Node2Tile-1 || p.y & Node2Tile-1)){
		werrstr("mapspawn: building spawn %P not aligned to tile map", p);
		return nil;
	}
	if(getspawn(&p, o) < 0)
		return nil;
	mo = emalloc(sizeof *mo);
	mo->uuid = lrand();
	mo->Point = p;
	mo->px = p.x * Nodewidth;
	mo->py = p.y * Nodeheight;
	mo->subpx = mo->px << Subpxshift;
	mo->subpy = mo->py << Subpxshift;
	mo->o = o;
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
