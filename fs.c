#include <u.h>
#include <libc.h>
#include <ctype.h>
#include <draw.h>
#include <bio.h>
#include "dat.h"
#include "fns.h"

Resource resource[Nresource];

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
	int frm;
	int type;
	int teamcol;
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
static Terrainl terrainl0 = {.l = &terrainl0}, *terrainl = &terrainl0;
static Pic tilesetpic;
static Picl pic0 = {.l = &pic0}, *pic = &pic0;
static Objp *objp;
static Attack *attack;
static Obj *obj;
static char *tileset;
static int nattack, nobj, nresource, nobjp;
static u32int bgcol = 0x00ffff;
static int rot17idx[17] = {
	0,2,4,6,8,10,12,14,16,17,19,21,23,25,27,29,31
};

static void
loadpic(char *name, Pic *pic, int alpha)
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
	if(alpha && i->chan != ARGB32 || !alpha && i->chan != RGB24)
		sysfatal("loadpic %s: inappropriate image format", name);
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
		if(alpha)
			v |= s[3] << 24;
		else if(v != bgcol)
			v |= 0xff << 24;
		*p++ = v;
		s += i->depth / 8;
	}
	free(b);
}

static void
loadobjpic(Pic *pic, Picl *pl, char *suff)
{
	int n, i, j;
	char path[128];
	u32int *p0;
	Pic pic0;

	for(i=0; i<pl->nr; i++){
		snprint(path, sizeof path, "%s.%02d.%02d%s.bit",
			pl->name, pl->frm, rot17idx[i], suff);
		loadpic(path, &pic0, pl->type & PFalpha);
		if(!pl->teamcol){		
			memcpy(pic++, &pic0, sizeof *pic);
			continue;
		}
		if(pic0.h % Nteam != 0)
			sysfatal("loadobjpic: obj %s sprite sheet %d,%d: height not multiple of %d",
				pl->name, pic0.w, pic0.h, Nteam);
		pic0.h /= Nteam;
		n = pic0.w * pic0.h;
		/* nteam has been set by now, no point in retaining sprites
		 * for additional teams */
		for(j=0, p0=pic0.p; j<nteam; j++, p0+=n, pic++){
			memcpy(pic, &pic0, sizeof *pic);
			pic->p = emalloc(n * sizeof *pic->p);
			memcpy(pic->p, p0, n * sizeof *pic->p);
		}
		free(pic0.p);
	}
}

static void
loadterpic(Pic *pic, Picl *pl)
{
	int id, size;
	char path[128];

	if(tilesetpic.p == nil){
		snprint(path, sizeof path, "%s.bit", tileset);
		loadpic(path, &tilesetpic, 0);
		if(tilesetpic.h % tilesetpic.w != 0)
			sysfatal("loadterpic: tiles not squares: tilepic %d,%d",
				tilesetpic.w, tilesetpic.h);
	}
	id = pl->frm;
	size = tilesetpic.w;
	if(size * id >= tilesetpic.h)
		sysfatal("loadterpic: terrain tile index %d out of bounds", id);
	pic->w = size;
	pic->h = size;
	size *= size;
	pic->p = emalloc(size * sizeof *pic->p);
	memcpy(pic->p, tilesetpic.p + size * id, size * sizeof *pic->p);
}

void
initimg(void)
{
	Pic *p;
	Picl *pl;

	for(pl=pic->l; pl!=pic; pl=pic->l){
		p = pl->p;
		if(pl->type & PFterrain)
			loadterpic(p, pl);
		else if(pl->type & PFshadow)
			loadobjpic(p, pl, ".s");
		else if(pl->type & PFglow)
			loadobjpic(p, pl, ".g");
		else
			loadobjpic(p, pl, "");
		pic->l = pl->l;
		free(pl);
	}
	free(tilesetpic.p);
}

static Pic *
pushpic(char *name, int frm, int type, int nr, int hasteam)
{
	int n;
	char iname[64];
	Picl *pl;

	snprint(iname, sizeof iname, "%s%02d%02ux", name, frm, type);
	for(pl=pic->l; pl!=pic; pl=pl->l)
		if(strcmp(iname, pl->iname) == 0)
			break;
	if(pl == pic){
		pl = emalloc(sizeof *pl);
		memcpy(pl->iname, iname, nelem(pl->iname));
		pl->frm = frm;
		pl->type = type;
		pl->name = name;
		pl->nr = nr;
		pl->teamcol = hasteam;
		if(nr != 17 && nr != 1)
			sysfatal("pushpic %s: invalid number of rotations", iname);
		n = nr;
		/* nteam isn't guaranteed to be set correctly by now, so
		 * just set to max */
		if(hasteam)
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

	for(tl=terrainl->l; tl!=terrainl; tl=tl->l)
		if(tl->id == id)
			break;
	if(tl == terrainl){
		tl = emalloc(sizeof *tl);
		tl->id = id;
		tl->t = emalloc(sizeof *tl->t);
		tl->t->p = pushpic("/tile/", id - 1, PFterrain, 1, 0);
		tl->l = terrainl->l;
		terrainl->l = tl;
	}
	return tl->t;
}

static void
vunpack(char **fld, char *fmt, va_list a)
{
	int n;
	double d;
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
		case 'f':
			if((d = strtod(*fld++, nil)) < 0.0)
				sysfatal("vunpack: illegal positive double %f", d);
			*va_arg(a, double*) = d;
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
				if(o->name != nil && strcmp(s, o->name) == 0)
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
	Terrain **t;

	if(tab->row == 0){
		tab->ncol = n;
		terwidth = n;
		terrain = emalloc(terheight * terwidth * sizeof *terrain);
	}
	t = terrain + tab->row * terwidth;
	for(x=0; x<n; x++, t++)
		unpack(fld++, "t", t);
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
	unpack(fld, "ddddddddddaaffff", &o->f, &o->w, &o->h,
		&o->hp, &o->def, &o->vis,
		o->cost, o->cost+1, o->cost+2, &o->time,
		o->atk, o->atk+1, &o->speed, &o->accel, &o->halt, &o->turn);
	o->accel /= 256.0;
	o->halt /= 256.0;
	/* halting distance in path node units */
	o->halt /= Tlsubwidth;
	if(o->w < 1 || o->h < 1)
		sysfatal("readobj: %s invalid dimensions %d,%d", o->name, o->w, o->h);
}

static void
readspr(char **fld, int n, Table *)
{
	int type, frm, nr;
	Obj *o;
	Pics *ps;
	Pic ***ppp, **p, **pe;

	if(n < 4)
		sysfatal("readspr %s: %d fields < 4 mandatory columns", o->name, n);
	unpack(fld, "odd", &o, &type, &nr);
	fld += 3;
	n -= 3;
	ps = nil;
	switch(type & 0xf){
	case PFidle: ps = o->pics[OSidle]; break;
	case PFmove: ps = o->pics[OSmove]; break;
	default: sysfatal("readspr %s: invalid type %#02ux", o->name, type & 0x7e);
	}
	if(type & PFshadow)
		ps += PTshadow;
	else if(type & PFglow)
		ps += PTglow;
	else
		ps += PTbase;
	ppp = &ps->pic;
	if(*ppp != nil)
		sysfatal("readspr %s: pic type %#ux already allocated", o->name, type);
	if(ps->nf != 0 && ps->nf != n || ps->nr != 0 && ps->nr != nr)
		sysfatal("readspr %s: spriteset phase error", o->name);
	ps->teamcol = (type & (PFshadow|PFterrain|PFglow)) == 0;
	ps->nf = n;
	ps->nr = nr;
	p = emalloc(n * sizeof *ppp);
	*ppp = p;
	for(pe=p+n; p<pe; p++){
		unpack(fld++, "d", &frm);
		*p = pushpic(o->name, frm, type, nr, ps->teamcol);
	}
}

enum{
	Tmapobj,
	Tobj,
	Tattack,
	Tresource,
	Tspawn,
	Ttileset,
	Tmap,
	Tspr,
};
Table table[] = {
	[Tmapobj] {"mapobj", readmapobj, 4, &nobjp},
	[Tobj] {"obj", readobj, 17, &nobj},
	[Tattack] {"attack", readattack, 4, &nattack},
	[Tresource] {"resource", readresource, 2, &nresource},
	[Tspawn] {"spawn", readspawn, -1, nil},
	[Ttileset] {"tileset", readtileset, 1, nil},
	[Tmap] {"map", readmap, -1, &terheight},
	[Tspr] {"spr", readspr, -1, nil},
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
		if(s[0] == 0 || s[0] == '#' || (p = strchr(s, ',')) == nil)
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
		if(s[0] == 0 || s[0] == '#' || (p = strchr(s, ',')) == nil)
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

	for(op=objp; op<objp+nobjp; op++)
		if(spawn(op->x * Tlnsub, op->y * Tlnsub, op->o, op->team) < 0)
			sysfatal("initmapobj: %s team %d: %r", op->o->name, op->team);
	free(objp);
}

static void
cleanup(void)
{
	Terrainl *tl;

	for(tl=terrainl->l; tl!=terrainl; tl=terrainl->l){
		terrainl->l = tl->l;
		free(tl);
	}
}

static void
fixobjspr(void)
{
	Obj *o;
	Pics *idle, *move;

	for(o=obj; o<obj+nobj; o++){
		if(o->f & Fbuild)
			continue;
		idle = o->pics[OSidle];
		move = o->pics[OSmove];
		if(idle[PTbase].pic == nil && move[PTbase].pic == nil)
			sysfatal("obj %s: no base sprites loaded", o->name);
		if(idle[PTbase].pic == nil){
			memcpy(idle+PTbase, move+PTbase, sizeof *idle);
			memcpy(idle+PTshadow, move+PTshadow, sizeof *idle);
			idle[PTbase].iscopy = 1;
			idle[PTshadow].iscopy = 1;
		}else if(move[PTbase].pic == nil){
			memcpy(move+PTbase, idle+PTbase, sizeof *move);
			memcpy(move+PTshadow, idle+PTshadow, sizeof *move);
			move[PTbase].iscopy = 1;
			move[PTshadow].iscopy = 1;
		}
	}
}

static void
checkdb(void)
{
	if(tileset == nil)
		sysfatal("checkdb: no tileset defined");
	if(nresource != Nresource)
		sysfatal("checkdb: incomplete resource specification");
	if(terwidth % 16 != 0 || terheight % 16 != 0 || terwidth * terheight == 0)
		sysfatal("checkdb: map size %d,%d not in multiples of 16",
			terwidth, terheight);
	if(nteam < 2)
		sysfatal("checkdb: not enough teams");
}

static void
initdb(void)
{
	checkdb();
	initmap();
	initmapobj();
	cleanup();
	fixobjspr();
}

void
initfs(void)
{
	if(bind(".", prefix, MBEFORE|MCREATE) == -1 || chdir(prefix) < 0)
		fprint(2, "init: %r\n");
	loaddb(dbname);
	loaddb(mapname);
	initdb();
}
