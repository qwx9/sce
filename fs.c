#include <u.h>
#include <libc.h>
#include <ctype.h>
#include <draw.h>
#include <bio.h>
#include "dat.h"
#include "fns.h"

/* FIXME: building sight range, set to 1 for now */

Resource resource[Nresource];
Map *map;
int mapwidth, mapheight;

typedef struct Table Table;
typedef struct Objp Objp;
typedef struct Picl Picl;
typedef struct Terrainl Terrainl;
struct Objp{
	Obj *o;
	int team;
	int x;
	int y;
};
struct Table{
	char name[64];
	void (*readfn)(char**, int, Table*);
	int ncol;
	int *nrow;
	int row;
};
struct Picl{
	int id;
	int type;
	char *name;
	char iname[64];
	int nr;
	Pic *p;
	Picl *l;
};
struct Terrainl{
	int id;
	Terrain *t;
	Terrainl *l;
};
static Terrainl terrain0 = {.l = &terrain0}, *terrain = &terrain0;
static Picl pic0 = {.l = &pic0}, *pic = &pic0;
static Objp *objp;
static Attack *attack;
static Obj *obj;
static char *tileset;
static int nattack, nobj, nresource, nobjp;
static u32int bgcol = 0x00ffff;

static void
loadpic(char *name, Pic *pic)
{
	int fd, n, m, dx, dy;
	Image *i;
	uchar *b, *s;
	u32int v, *p;

	if((fd = open(name, OREAD)) < 0)
		sysfatal("loadpic: %r");
	if((i = readimage(display, fd, 0)) == nil)
		sysfatal("readimage: %r");
	close(fd);
	if(i->chan != RGB24)
		sysfatal("loadpic %s: non-RGB24 image", name);
	dx = Dx(i->r);
	dy = Dy(i->r);
	n = dx * dy;
	m = n * i->depth / 8;
	b = emalloc(m);
	unloadimage(i, i->r, b, m);
	p = emalloc(n * sizeof *p);
	pic->p = p;
	pic->w = dx;
	pic->h = dy;
	pic->dx = i->r.min.x;
	pic->dy = i->r.min.y;
	freeimage(i);
	s = b;
	while(n-- > 0){
		v = s[2] << 16 | s[1] << 8 | s[0];
		if(v != bgcol)
			v |= 0xff << 24;
		*p++ = v;
		s += 3;
	}
	free(b);
}

void
initimg(void)
{
	int i, r;
	char path[128];
	Pic *p;
	Picl *pl;

	for(pl=pic->l; pl!=pic; pl=pic->l){
		p = pl->p;
		if(pl->type == PFterrain){
			snprint(path, sizeof path, "%s.%05d.bit", tileset, pl->id);
			loadpic(path, p);
		}else if(pl->type & PFshadow){
			for(r=0; r<pl->nr; r++){
				snprint(path, sizeof path,
					"%ss.%02d.%02d.bit",
					pl->name, pl->id, r);
				loadpic(path, p++);
			}
		}else{
			for(i=0; i<nteam; i++)
				for(r=0; r<pl->nr; r++){
					snprint(path, sizeof path,
						"%s%d.%02d.%02d.bit",
						pl->name, i+1, pl->id, r);
					loadpic(path, p++);
				}
		}
		pic->l = pl->l;
		free(pl);
	}
}

static Pic *
pushpic(char *name, int id, int type, int nr)
{
	int n;
	char iname[64];
	Picl *pl;

	snprint(iname, sizeof iname, "%s%d%02ux", name, id, type);
	for(pl=pic->l; pl!=pic; pl=pl->l)
		if(strcmp(iname, pl->iname) == 0)
			break;
	if(pl == pic){
		pl = emalloc(sizeof *pl);
		memcpy(pl->iname, iname, nelem(pl->iname));
		pl->id = id;
		pl->type = type;
		pl->name = name;
		pl->nr = nr;
		n = nr;
		if((type & PFshadow) == 0)
			n *= Nteam;
		pl->p = emalloc(n * sizeof *pl->p);
		pl->l = pic->l;
		pic->l = pl;
	}
	return pl->p;
}

static Terrain *
pushterrain(int id)
{
	Terrainl *tl;

	for(tl=terrain->l; tl!=terrain; tl=tl->l)
		if(tl->id == id)
			break;
	if(tl == terrain){
		tl = emalloc(sizeof *tl);
		tl->id = id;
		tl->t = emalloc(sizeof *tl->t);
		tl->t->p = pushpic(",", id, PFterrain, 1);
		tl->l = terrain->l;
		terrain->l = tl;
	}
	return tl->t;
}

static void
vunpack(char **fld, char *fmt, va_list a)
{
	int n;
	char *s;
	Attack *atk;
	Resource *r;
	Obj *o;

	for(;;){
		switch(*fmt++){
		default: sysfatal("unknown format %c", fmt[-1]);
		case 0: return;
		case 'd':
			if((n = strtol(*fld++, nil, 0)) < 0)
				sysfatal("vunpack: illegal positive integer %d", n);
			*va_arg(a, int*) = n;
			break;
		case 'a':
			s = *fld++;
			if(*s == 0){
				*va_arg(a, Attack**) = nil;
				break;
			}
			for(atk=attack; atk<attack+nattack; atk++)
				if(strcmp(s, atk->name) == 0)
					break;
			if(atk == attack + nattack)
				sysfatal("vunpack: no such attack %s", s);
			*va_arg(a, Attack**) = atk;
			break;
		case 'r':
			s = *fld++;
			if(*s == 0){
				*va_arg(a, Resource**) = nil;
				break;
			}
			for(r=resource; r<resource+nelem(resource); r++)
				if(strcmp(s, r->name) == 0)
					break;
			if(r == resource + nelem(resource))
				sysfatal("vunpack: no such resource %s", s);
			*va_arg(a, Resource**) = r;
			break;
		case 'o':
			s = *fld++;
			if(*s == 0)
				sysfatal("vunpack: empty obj");
			for(o=obj; o<obj+nobj; o++)
				if(strcmp(s, o->name) == 0)
					break;
			if(o == obj + nobj)
				sysfatal("vunpack: no such obj %s", s);
			*va_arg(a, Obj**) = o;
			break;
		case 't':
			s = *fld++;
			if(*s == 0)
				sysfatal("vunpack: empty terrain");
			if((n = strtol(s, nil, 0)) <= 0)
				sysfatal("vunpack: illegal terrain index %d", n);
			*va_arg(a, Terrain**) = pushterrain(n);
			break;
		}
	}
}

static void
unpack(char **fld, char *fmt, ...)
{
	va_list a;

	va_start(a, fmt);
	vunpack(fld, fmt, a);
	va_end(a);
}

static void
readspawn(char **fld, int n, Table *)
{
	Obj *o, **os, **oe;

	unpack(fld++, "o", &o);
	if(o->spawn != nil)
		sysfatal("readspawn: spawn already assigned for obj %s", *fld);
	o->spawn = emalloc(--n * sizeof *o->spawn);
	o->nspawn = n;
	for(os=o->spawn, oe=os+n; os<oe; os++)
		unpack(fld++, "o", os);
}

static void
readtileset(char **fld, int, Table *)
{
	if(tileset != nil)
		sysfatal("readtileset %s: already defined as %s", *fld, tileset);
	tileset = estrdup(*fld);
}

static void
readmap(char **fld, int n, Table *tab)
{
	int x;
	Map *m;

	if(tab->row == 0){
		tab->ncol = n;
		mapwidth = n;
		map = emalloc(mapheight * mapwidth * sizeof *map);
	}
	m = map + tab->row * mapwidth;
	for(x=0; x<n; x++, m++){
		unpack(fld++, "t", &m->t);
		/* FIXME: get rid of these */
		m->tx = x;
		m->ty = tab->row;
		m->x = m->tx * Tlwidth;
		m->y = m->ty * Tlheight;
		m->lo.lo = m->lo.lp = &m->lo;
	}
}

static void
readmapobj(char **fld, int, Table *tab)
{
	Objp *op;

	if(objp == nil)
		objp = emalloc(nobjp * sizeof *objp);
	op = objp + tab->row;
	unpack(fld, "oddd", &op->o, &op->team, &op->x, &op->y);
	if(op->team > nelem(team))
		op->team = 0;
	if(op->team > nteam)
		nteam = op->team;
}

static void
readresource(char **fld, int, Table *tab)
{
	Resource *r;

	r = resource + tab->row;
	if(r >= resource + nelem(resource))
		sysfatal("readresource: out of bounds reference");
	r->name = estrdup(*fld++);
	unpack(fld, "d", &r->init);
}

static void
readattack(char **fld, int, Table *tab)
{
	Attack *a;

	if(attack == nil)
		attack = emalloc(nattack * sizeof *attack);
	a = attack + tab->row;
	a->name = estrdup(*fld++);
	unpack(fld, "ddd", &a->dmg, &a->range, &a->cool);
}

static void
readobj(char **fld, int, Table *tab)
{
	Obj *o;

	if(obj == nil)
		obj = emalloc(nobj * sizeof *obj);
	o = obj + tab->row;
	o->name = estrdup(*fld++);
	unpack(fld, "ddddddddddddaa", &o->nr, &o->f, &o->w, &o->h,
		&o->hp, &o->def, &o->speed, &o->vis,
		o->cost, o->cost+1, o->cost+2, &o->time,
		o->atk, o->atk+1);
}

static void
readspr(char **fld, int n, Table *)
{
	int type, id;
	Obj *o;
	Pics *ps;
	Pic ***ppp, **p, **pe;

	unpack(fld, "od", &o, &type);
	fld += 2;
	n -= 2;
	ps = nil;
	switch(type & 0x7f){
	case PFidle: ps = &o->pidle; break;
	case PFmove: ps = &o->pmove; break;
	default: sysfatal("readspr: invalid type %#02ux", type & 0x7f);
	}
	ppp = type & PFshadow ? &ps->shadow : &ps->p;
	if(*ppp != nil)
		sysfatal("readspr: %s pic type %#ux already allocated", o->name, type);
	if(ps->nf != 0 && ps->nf != n)
		sysfatal("readspr: %s spriteset phase error", o->name);
	ps->nf = n;
	p = emalloc(n * sizeof *ppp);
	*ppp = p;
	for(pe=p+n; p<pe; p++){
		unpack(fld++, "d", &id);
		*p = pushpic(o->name, id, type, o->nr);
	}
}

Table table[] = {
	{"mapobj", readmapobj, 4, &nobjp},
	{"obj", readobj, 15, &nobj},
	{"attack", readattack, 4, &nattack},
	{"resource", readresource, 2, &nresource},
	{"spawn", readspawn, -1, nil},
	{"tileset", readtileset, 1, nil},
	{"map", readmap, -1, &mapheight},
	{"spr", readspr, -1, nil},
};

static int
getcsvfields(char *s, char **fld, int nfld)
{
	int n;

	if(nfld < 1)
		return 0;
	n = 0;
	*fld++ = s;
	if(++n == nfld)
		return n;
	while(*s != 0){
		if(*s == ','){
			*s++ = 0;
			*fld++ = s;
			if(++n == nfld)
				return n;
		}else
			s++;
	}
	return n;
}

static void
loaddb(char *path)
{
	int n;
	char *s, *p, *fld[256];
	Biobuf *bf;
	Table *t;

	if((bf = Bopen(path, OREAD)) == nil)
		sysfatal("loaddb: %r");
	Blethal(bf, nil);
	/* parse twice to preallocate tables, otherwise cross-references will be
	 * invalid */
	while((s = Brdstr(bf, '\n', 1)) != nil){
		if((p = strchr(s, ',')) == nil)
			goto skip;
		if(p == s || p[1] == 0)
			goto skip;
		*p = 0;
		for(t=table; t<table+nelem(table); t++)
			if(strcmp(s, t->name) == 0)
				break;
		if(t == table + nelem(table))
			sysfatal("loaddb: unknown table %s", s);
		if(t->nrow != nil)
			(*t->nrow)++;
	skip:
		free(s);
	}
	Bseek(bf, 0, 0);
	while((s = Brdstr(bf, '\n', 1)) != nil){
		if((p = strchr(s, ',')) == nil)
			goto next;
		if(p == s || p[1] == 0)
			goto next;
		*p = 0;
		for(t=table; t<table+nelem(table); t++)
			if(strcmp(s, t->name) == 0)
				break;
		n = getcsvfields(p+1, fld, nelem(fld));
		if(n != t->ncol && t->ncol >= 0)
			sysfatal("loaddb: invalid row length %d for %s record %s", n, s, fld[0]);
		t->readfn(fld, n, t);
		t->row++;
	next:
		free(s);
	}
	Bterm(bf);
}

static void
initmapobj(void)
{
	Objp *op;
	Map *m;

	for(op=objp; op<objp+nobjp; op++){
		m = map + mapwidth * op->y + op->x;
		if(spawn(m, op->o, op->team) < 0)
			sysfatal("initmapobj: %s team %d: %r", op->o->name, op->team);
	}
	free(objp);
}

static void
cleanup(void)
{
	Terrainl *tl;

	for(tl=terrain->l; tl!=terrain; tl=terrain->l){
		terrain->l = tl->l;
		free(tl);
	}
}

static void
checkdb(void)
{
	if(tileset == nil)
		sysfatal("checkdb: no tileset defined");
	if(nresource != Nresource)
		sysfatal("checkdb: incomplete resource specification");
	if(mapwidth < 8 || mapheight < 8)
		sysfatal("checkdb: map too small %d,%d", mapwidth, mapheight);
	if(nteam < 2)
		sysfatal("checkdb: not enough teams");
}

static void
initdb(void)
{
	initpath();
	initmapobj();
	checkdb();
	cleanup();
}

void
init(void)
{
	if(bind(".", prefix, MBEFORE|MCREATE) < 0 || chdir(prefix) < 0)
		fprint(2, "init: %r\n");
	loaddb(dbname);
	loaddb(mapname);
	initdb();
}
