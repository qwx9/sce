#include <u.h>
#include <libc.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

Tile *tilemap;
int tilemapwidth, tilemapheight;
Node *map;
int mapwidth, mapheight;

void
setpos(Mobj *mo, Point p)
{
	assert(p.x < mapwidth && p.y < mapheight);
	mo->Point = p;
	mo->sub.x = mo->x << Subshift;
	mo->sub.y = mo->y << Subshift;
}

void
setsubpos(Mobj *mo, Point p)
{
	mo->sub = p;
	mo->x = p.x >> Subshift;
	mo->y = p.y >> Subshift;
}

void
snaptomapgrid(Mobj *mo)
{
	markmobj(mo, 0);
	setpos(mo, mo->Point);
	markmobj(mo, 1);
}

Tile *
tilepos(Point p)
{
	p = divpt(p, Node2Tile);
	return tilemap + p.y * tilemapwidth + p.x;
}

void
linktomap(Mobj *mo)
{
	Tile *t;

	t = tilepos(mo->Point);
	mo->mapl = linkmobj(mo->o->f & Fair ? t->ml.lp : &t->ml, mo, mo->mapl);
}

static void
updatemap(Mobj *mo)
{
	Mobj *bmo;

	if(isblocked(mo->Point, mo->o)){
		bmo = unitat(mo->Point);
		sysfatal("markmobj: attempt to place %s at %P, non-free block having %s at %P",
			mo->o->name, mo->Point, bmo->o->name, bmo->Point);
	}
	linktomap(mo);
	markmobj(mo, 1);
}

static int
findspawn(Point *pp, int ofs, Obj *o, Obj *spawn)
{
	Rectangle r;
	Point p;

	p = *pp;
	r.min.x = p.x - (ofs+1) * o->w;
	r.min.y = p.y - (ofs+1) * o->h;
	r.max.x = p.x + spawn->w + ofs * o->w;
	r.max.y = p.y + spawn->h + ofs * o->h;
	for(p.x=r.min.x+o->w, p.y=r.max.y; p.x<r.max.x; p.x++)
		if(!isblocked(p, o))
			goto found;
	for(p.x=r.max.x, p.y=r.max.y; p.y>r.min.y; p.y--)
		if(!isblocked(p, o))
			goto found;
	for(p.x=r.max.x, p.y=r.min.y; p.x>r.min.x; p.x--)
		if(!isblocked(p, o))
			goto found;
	for(p.x=r.min.x, p.y=r.min.y; p.y<=r.max.y; p.y++)
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
	Tile *t;
	Mobjl *ml;
	Mobj *mo;
	Obj **os;

	p = *pp;
	if(o->f & (Fbuild|Fimmutable)){
		if((p.x & Node2Tile - 1) || (p.y & Node2Tile - 1)){
			werrstr("getspawn: unaligned building placement %P", p);
			return -1;
		}
		if(isblocked(p, o)){
			werrstr("getspawn: building placement at %P blocked", p);
			return -1;
		}
	}else{
		t = tilepos(p);
		for(mo=nil, ml=t->ml.l; ml!=&t->ml; ml=ml->l){
			mo = ml->mo;
			for(os=mo->o->spawn, n=mo->o->nspawn; n>0; n--, os++)
				if(*os == o)
					break;
			if(n > 0)
				break;
		}
		if(ml == &t->ml){
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
mapspawn(Obj *o, Point p)
{
	Mobj *mo;

	if(getspawn(&p, o) < 0)
		return nil;
	mo = emalloc(sizeof *mo);
	mo->uuid = lrand();
	setpos(mo, p);
	mo->o = o;
	newvec(&mo->path.moves, 32, sizeof(Point));
	updatemap(mo);
	return mo;
}

void
initmap(void)
{
	mapwidth = tilemapwidth * Node2Tile;
	mapheight = tilemapheight * Node2Tile;
	map = emalloc(mapwidth * mapheight * sizeof *map);
	initbmap();
}
