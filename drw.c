#include <u.h>
#include <libc.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

extern QLock drawlock;

int scale = 1;
static Point p0, pan;

static int fbsz, fbh, fbw, fbws;
static u32int *fb, *fbvis;
static Image *fbi;
static Rectangle selr;
static Point panmax;
static Mobj *selected[Nselect];
static Vector vis;

enum{
	DLgndshad,
	DLgnd,
	DLgndglow,
	DLairshad,
	DLair,
	DLairglow,
	DLend,
};
typedef struct Drawlist Drawlist;
struct Drawlist{
	Vector mobj;
	Vector pics;
	int noalpha;
};
static Drawlist drawlist[DLend] = {
	[DLgnd] {.noalpha 1},
	[DLair] {.noalpha 1},
};

void
dopan(Point p)
{
	pan.x -= p.x;
	pan.y -= p.y;
	if(pan.x < 0)
		pan.x = 0;
	else if(pan.x > panmax.x)
		pan.x = panmax.x;
	if(pan.y < 0)
		pan.y = 0;
	else if(pan.y > panmax.y)
		pan.y = panmax.y;
}

static Mobj *
vismobj(Point p)
{
	int i;
	Mobj **mp;

	if((i = fbvis[p.y * fbw + p.x]) < 0)
		return nil;
	mp = vis.p;
	assert(i < vis.n);
	return mp[i];
}

void
doselect(Point p)
{
	if(!ptinrect(p, selr))
		return;
	p = divpt(subpt(p, selr.min), scale);
	selected[0] = vismobj(p);
}

void
doaction(Point p, int clearcmds)
{
	Mobj *mo, *tgt;

	mo = selected[0];
	if(mo == nil || mo->o->f & Fimmutable || !ptinrect(p, selr))
		return;
	p = subpt(p, selr.min);
	tgt = vismobj(divpt(p, scale));
	p = divpt(addpt(p, pan), scale);
	p = divpt(p, Nodesz);
	if(p.x + mo->o->w > mapwidth || p.y + mo->o->h > mapheight){
		dprint("doaction: %M target %P beyond map edge\n", mo, p);
		return;
	}
	if(tgt == mo || eqpt(mo->Point, p)){
		dprint("doaction: %M targeting moself\n", mo);
		return;
	}
	if(clearcmds)
		sendstop(mo);
	if(tgt != nil){
		if((tgt->o->f & Fresource) && (mo->o->f & Fgather))
			sendgather(mo, tgt);
		else
			sendmovenear(mo, tgt);
	}else
		sendmove(mo, p);
}

static void
drawhud(void)
{
	int i;
	char s[256], *s´;
	Point p;
	Mobj *mo;
	Team *t;

	p = p0;
	draw(screen, Rpt(p, screen->r.max), display->black, nil, ZP);
	mo = selected[0];
	if(mo == nil)
		return;
	if(mo->o->f & Fresource)
		snprint(s, sizeof s, "%s %d", mo->o->name, mo->amount);
	else
		snprint(s, sizeof s, "%s %d/%d", mo->o->name, mo->hp, mo->o->hp);
	string(screen, p, display->white, ZP, font, s);
	p.y += font->height;
	if((mo->o->f & Fresource) == 0){
		snprint(s, sizeof s, "%s", mo->state < OSend ? statename[mo->state] : "");
		string(screen, p, display->white, ZP, font, s);
	}
	p.y += font->height;
	t = teams + mo->team;
	if(t == teams)
		return;
	s´ = seprint(s, s+sizeof s, "team %d: ", mo->team);
	for(i=0; i<nelem(t->r); i++)
		s´ = seprint(s´, s+sizeof s, "[%s] %d ", resources[i].name, t->r[i]);
	string(screen, p, display->white, ZP, font, s);
}

static void
clearvis(void)
{
	clearvec(&vis);
}

static int
addvis(Mobj *mo)
{
	pushvec(&vis, &mo);
	return vis.n - 1;
}

static int
boundpic(Rectangle *rp, Point o, u32int **q)
{
	int w;
	Rectangle r;

	r = *rp;
	r.min = addpt(r.min, o);
	r.min.x -= pan.x / scale;
	r.min.y -= pan.y / scale;
	if(r.min.x + r.max.x < 0 || r.min.x >= fbw
	|| r.min.y + r.max.y < 0 || r.min.y >= fbh)
		return -1;
	w = r.max.x;
	if(r.min.x < 0){
		if(q != nil)
			*q -= r.min.x;
		r.max.x += r.min.x;
		r.min.x = 0;
	}
	if(r.min.x + r.max.x > fbw)
		r.max.x -= r.min.x + r.max.x - fbw;
	if(r.min.y < 0){
		if(q != nil)
			*q -= w * r.min.y;
		r.max.y += r.min.y;
		r.min.y = 0;
	}
	if(r.min.y + r.max.y > fbh)
		r.max.y -= r.min.y + r.max.y - fbh;
	r.min.x *= scale;
	r.max.x *= scale;
	*rp = r;
	return 0;
}

static void
drawpic(Point o, Pic *pic, int ivis)
{
	int n, Δp, Δsp, Δq;
	u32int v, *p, *e, *sp, *q;
	Rectangle r;

	if(pic->p == nil)
		sysfatal("drawpic: empty pic");
	q = pic->p;
	r = Rect(pic->Δ.x, pic->Δ.y, pic->w, pic->h);
	if(boundpic(&r, o, &q) < 0)
		return;
	Δq = pic->w - r.max.x / scale;
	p = fb + r.min.y * fbws + r.min.x;
	Δp = fbws - r.max.x;
	sp = fbvis + r.min.y * fbw + r.min.x / scale;
	Δsp = fbw - r.max.x / scale;
	while(r.max.y-- > 0){
		e = p + r.max.x;
		while(p < e){
			v = *q++;
			if(v & 0xff << 24){
				for(n=0; n<scale; n++)
					*p++ = v;
			}else
				p += scale;
			*sp++ = ivis;
		}
		q += Δq;
		p += Δp;
		sp += Δsp;
	}
}

static void
drawpicalpha(Point o, Pic *pic)
{
	int n, Δp, Δq;
	u8int k, a, b;
	u32int f, A, B, *p, *e, *q;
	Rectangle r;

	if(pic->p == nil)
		sysfatal("drawpicalpha: empty pic");
	q = pic->p;
	r = Rect(pic->Δ.x, pic->Δ.y, pic->w, pic->h);
	if(boundpic(&r, o, &q) < 0)
		return;
	Δq = pic->w - r.max.x / scale;
	p = fb + r.min.y * fbws + r.min.x;
	Δp = fbws - r.max.x;
	while(r.max.y-- > 0){
		e = p + r.max.x;
		while(p < e){
			A = *q++;
			k = A >> 24;
			B = *p;
			for(n=0; n<24; n+=8){
				a = A >> n;
				b = B >> n;
				f = k * (a - b);
				f = (f + 1 + (f >> 8)) >> 8;
				B = B & ~(0xff << n) | (f + b & 0xff) << n;
			}
			for(n=0; n<scale; n++)
				*p++ = B;
		}
		q += Δq;
		p += Δp;
	}
}

void
compose(Point o, u32int c)
{
	int n, Δp;
	u32int v, *p, *e;
	Rectangle r;

	r = Rpt(ZP, Pt(Nodesz, Nodesz));
	if(boundpic(&r, o, nil) < 0)
		return;
	p = fb + r.min.y * fbws + r.min.x;
	Δp = fbws - r.max.x;
	while(r.max.y-- > 0){
		e = p + r.max.x;
		while(p < e){
			v = *p;
			v = (v & 0xff0000) + (c & 0xff0000) >> 1 & 0xff0000
				| (v & 0xff00) + (c & 0xff00) >> 1 & 0xff00
				| (v & 0xff) + (c & 0xff) >> 1 & 0xff;
			for(n=0; n<scale; n++)
				*p++ = v;
		}
		p += Δp;
	}
}

static Pic *
frm(Mobj *mo, int type)
{
	static int rot17[Nrot] = {
		0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,
		9,9,10,10,11,11,12,12,13,13,14,14,15,15,16
	};
	int n, θ, frm;
	Pics *pp;
	Pic *p;

	n = mo->state;
	if(n == OSskymaybe || n == OSwait)
		n = OSidle;
	if(n < 0 || n > OSend){
		dprint("frm: %M invalid animation frame %d\n", mo, n);
		return nil;
	}
	pp = &mo->o->pics[n][type];
	if(pp->pic == nil)
		return nil;
	frm = pp->freeze ? mo->freezefrm : tc % pp->nf;
	θ = mo->θ * 32.0 / 256;
	switch(pp->nr){
	case 17: θ = rot17[θ]; break;
	default: θ = 0; break;
	}
	p = pp->pic[frm];
	if(pp->teamcol)
		p += nteam * θ + mo->team - 1;
	else
		p += θ;
	return p;
}

static void
clearlists(void)
{
	Drawlist *dl;

	for(dl=drawlist; dl<drawlist+DLend; dl++){
		clearvec(&dl->mobj);
		clearvec(&dl->pics);
	}
}

static void
addpic(Drawlist *dl, Mobj *mo, int type)
{
	Pic *p;

	if((p = frm(mo, type)) == nil)
		return;
	pushvec(&dl->mobj, &mo);
	pushvec(&dl->pics, &p);
}

static void
addmobjs(Tile *t)
{
	int air;
	Mobj *mo;
	Mobjl *ml;

	for(ml=t->ml.l; ml!=&t->ml; ml=ml->l){
		mo = ml->mo;
		air = mo->o->f & Fair;
		addpic(drawlist + (air ? DLairshad : DLgndshad), mo, PTshadow);
		addpic(drawlist + (air ? DLair : DLgnd), mo, PTbase);
		if(mo->state == OSmove)
			addpic(drawlist + (air ? DLairglow : DLgndglow), mo, PTglow);
	}
}

static void
drawmobjs(void)
{
	int n;
	Mobj *mo, **mp;
	Pic **pp;
	Drawlist *dl;

	for(dl=drawlist; dl<drawlist+DLend; dl++)
		for(mp=dl->mobj.p, pp=dl->pics.p, n=0; n<dl->mobj.n; n++, mp++, pp++){
			mo = *mp;
			if(dl->noalpha)
				drawpic(Pt(mo->sub.x >> Pixelshift,
					mo->sub.y >> Pixelshift), *pp, addvis(mo));
			else
				drawpicalpha(Pt(mo->sub.x >> Pixelshift,
					mo->sub.y >> Pixelshift), *pp);
		}
}

static void
mapdrawrect(Rectangle *rp)
{
	Rectangle r;

	r.min = divpt(pan, scale);
	r.min = divpt(r.min, Tilesz);
	r.max.x = r.min.x + (pan.x / scale % Tilesz != 0);
	r.max.x += fbw / Tilesz + (fbw % Tilesz != 0);
	if(r.max.x > tilemapwidth)
		r.max.x = tilemapwidth;
	r.max.y = r.min.y + (pan.y / scale % Tilesz != 0);
	r.max.y += fbh / Tilesz + (fbh % Tilesz != 0);
	if(r.max.y > tilemapheight)
		r.max.y = tilemapheight;
	/* enlarge window to capture units overlapping multiple tiles;
	 * seems like the easiest way to take this into account */
	r.min.x = max(r.min.x - 4, 0);
	r.min.y = max(r.min.y - 4, 0);
	*rp = r;
}

static void
flushdrw(void)
{
	uchar *p;
	Rectangle r, r2;

	r = selr;
	p = (uchar *)fb;
	if(scale == 1){
		loadimage(fbi, fbi->r, p, fbsz);
		draw(screen, r, fbi, nil, ZP);
	}else{
		r2 = r;
		while(r.min.y < r2.max.y){
			r.max.y = r.min.y + scale;
			p += loadimage(fbi, fbi->r, p, fbsz / fbh);
			draw(screen, r, fbi, nil, ZP);
			r.min.y = r.max.y;
		}
	}
	flushimage(display, 1);
}

static void
redraw(void)
{
	Point p;
	Rectangle r;
	Tile *t;

	clearvis();
	clearlists();
	mapdrawrect(&r);
	t = tilemap + p.y * tilemapwidth + r.min.x;
	for(p.y=r.min.y; p.y<r.max.y; p.y++){
		for(p.x=r.min.x; p.x<r.max.x; p.x++, t++){
			drawpic(mulpt(p, Tilesz), t->t->p, -1);
			addmobjs(t);
		}
		t += tilemapwidth - (r.max.x - r.min.x);
	}
	drawmobjs();
	if(debugmap)
		drawnodemap(r, selected[0]);
	drawhud();
}

void
updatedrw(void)
{
	qlock(&drawlock);
	redraw();
	qunlock(&drawlock);
	flushdrw();
}

void
resetdrw(void)
{
	fbws = min(mapwidth * Nodesz * scale, Dx(screen->r));
	fbh = min(mapheight * Nodesz * scale, Dy(screen->r));
	selr = Rpt(screen->r.min, addpt(screen->r.min, Pt(fbws, fbh)));
	p0 = Pt(screen->r.min.x + 8, screen->r.max.y - 3 * font->height);
	p0.y -= (p0.y - screen->r.min.y) % scale;
	panmax.x = max(Nodesz * mapwidth * scale - Dx(screen->r), 0);
	panmax.y = max(Nodesz * mapheight * scale - Dy(screen->r), 0);
	if(p0.y < selr.max.y){
		panmax.y += selr.max.y - p0.y;
		fbh -= selr.max.y - p0.y;
		selr.max.y = p0.y;
	}
	fbw = fbws / scale;
	fbh /= scale;
	fbsz = fbws * fbh * sizeof *fb;
	free(fb);
	free(fbvis);
	freeimage(fbi);
	fb = emalloc(fbsz);
	fbvis = emalloc(fbw * fbh * sizeof *fb);
	if((fbi = allocimage(display,
		Rect(0,0,fbws,scale==1 ? fbh : 1),
		XRGB32, scale>1, 0)) == nil)
		sysfatal("allocimage: %r");
	draw(screen, screen->r, display->black, nil, ZP);
}

void
initdrw(void)
{
	Drawlist *dl;

	if(initdraw(nil, nil, "path") < 0)
		sysfatal("initdraw: %r");
	newvec(&vis, 32, sizeof(Mobj*));
	for(dl=drawlist; dl<drawlist+DLend; dl++){
		newvec(&dl->mobj, 32, sizeof(Mobj*));
		newvec(&dl->pics, 32, sizeof(Pic*));
	}
}
