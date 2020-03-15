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
static Objp *objp;
static Attack *attack;
static Obj *obj;
static char *tileset;
static Terrain terrain0 = {.t = &terrain0}, *terrain = &terrain0;
static int nattack, nobj, nresource, nobjp;
static u32int bgcol = 0x00ffff;

static void
loadpic(char *name, Pic *pic)
{
	int fd, n, m, dx, dy;
	Image *i;
	uchar *b, *s;
	u32int v, *p;

	if(name == nil || strlen(name) == 0)
		sysfatal("loadpic: invalid name");
	if((fd = open(name, OREAD)) < 0){
		fprint(2, "loadpic: %r\n");
		return;
	}
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

static int
getfrm(char *name, char *id)
{
	int f;
	char s[128];

	for(f=0;;f++){
		snprint(s, sizeof s, "%s1%s.%02d.00.bit", name, id, f);
		if(access(s, AREAD) < 0)
			return f;
	}
}

static int
loadpics(Pics *p, char *name, char *id, int nr)
{
	int i, r, f, nf;
	char s[128];
	Pic *pp;

	if((nf = getfrm(name, id)) == 0)
		return -1;
	p->nf = nf;
	p->nr = nr;
	p->n = nf * nr;
	pp = p->p = emalloc(nteam * nf * nr * sizeof *pp);
	for(i=0; i<nteam; i++){
		for(r=0; r<nr; r++)
			for(f=0; f<nf; f++){
				snprint(s, sizeof s, "%s%d%s.%02d.%02d.bit", name, i+1, id, f, r);
				loadpic(s, pp++);
			}
	}
	return 0;
}

void
initimg(void)
{
	int nr;
	char s[64];
	Terrain *t;
	Obj *o;

	for(t=terrain->t; t!=terrain; t=t->t){
		snprint(s, sizeof s, "%s.%05d.bit", tileset, t->n);
		loadpic(s, &t->pic);
		if(t->pic.w != Tlwidth || t->pic.h != Tlheight)
			sysfatal("initimg %s: invalid size %dx%d\n", s, t->pic.w, t->pic.h);
	}
	for(o=obj; o<obj+nobj; o++){
		nr = o->f & Fbuild ? 1 : Nrot;
		if(loadpics(&o->pidle, o->name, "", nr) < 0)
			sysfatal("initimg %s: no idle frames", o->name);
		loadpics(&o->pmove, o->name, "m", nr);
		loadpics(&o->patk, o->name, "a", nr);
		loadpics(&o->pgather, o->name, "g", nr);
		loadpics(&o->pburrow, o->name, "b", 1);
		loadpics(&o->pdie, o->name, "d", 1);
	}
}

static void
vunpack(char **fld, char *fmt, va_list a)
{
	int n;
	char *s;
	Attack *atk;
	Resource *r;
	Obj *o;
	Terrain *t;

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
			if(*s == 0){
				*va_arg(a, Obj**) = nil;
				break;
			}
			for(o=obj; o<obj+nobj; o++)
				if(strcmp(s, o->name) == 0)
					break;
			if(o == obj + nobj)
				sysfatal("vunpack: no such obj %s", s);
			*va_arg(a, Obj**) = o;
			break;
		case 't':
			s = *fld++;
			if(*s == 0){
				*va_arg(a, Terrain**) = nil;
				break;
			}
			if((n = strtol(s, nil, 0)) <= 0)
				sysfatal("vunpack: illegal terrain index %d", n);
			for(t=terrain->t; t!=terrain; t=t->t)
				if(t->n == n)
					break;
			if(t == terrain){
				t = emalloc(sizeof *t);
				t->n = n;
				t->t = terrain->t;
				terrain->t = t;
			}
			*va_arg(a, Terrain**) = t;
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
	if(o == nil)
		sysfatal("readspawn: empty string");
	if(o->spawn != nil)
		sysfatal("readspawn: spawn already assigned for obj %s", *fld);
	o->spawn = emalloc(--n * sizeof *o->spawn);
	o->nspawn = n;
	for(os=o->spawn, oe=os+n; os<oe; os++){
		unpack(fld++, "o", os);
		if(*os == nil)
			sysfatal("readspawn: empty string");
	}
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
	unpack(fld, "dddddddddddaa", &o->f, &o->w, &o->h,
		&o->hp, &o->def, &o->speed, &o->vis,
		o->cost, o->cost+1, o->cost+2, &o->time,
		o->atk, o->atk+1);
}

Table table[] = {
	{"mapobj", readmapobj, 4, &nobjp},
	{"obj", readobj, 14, &nobj},
	{"attack", readattack, 4, &nattack},
	{"resource", readresource, 2, &nresource},
	{"spawn", readspawn, -1, nil},
	{"tileset", readtileset, 1, nil},
	{"map", readmap, -1, &mapheight},
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
			sysfatal("loaddb: invalid row length %d for %s record", n, s);
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
