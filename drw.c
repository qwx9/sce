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
static Mobj **visbuf;
static int nvisbuf, nvis;

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
	Mobj **mo;
	Pic **pics;
	int n;
	int sz;
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

void
doselect(Point p)
{
	int i;

	if(!ptinrect(p, selr))
		return;
	p = divpt(subpt(p, selr.min), scale);
	i = fbvis[p.y * fbw + p.x];
	selected[0] = i == -1 ? nil : visbuf[i];
}

void
doaction(Point p, int clearcmds)
{
	int i;
	Point vp;
	Mobj *mo, *it;

	it = selected[0];
	if(it == nil || it->o->f & Fimmutable || !ptinrect(p, selr))
		return;
	vp = divpt(subpt(p, selr.min), scale);
	i = fbvis[vp.y * fbw + vp.x];
	mo = i == -1 ? nil : visbuf[i];
	if(mo == it){
		dprint("doaction: %M targeting itself\n", it);
		return;
	}
	p = divpt(addpt(subpt(p, selr.min), pan), scale);
	p.x /= Nodewidth;
	p.y /= Nodeheight;
	if(nodemapwidth - p.x < it->o->w || nodemapheight - p.y < it->o->h){
		dprint("doaction: %M destination beyond map edge\n", it);
		return;
	}
	if(clearcmds)
		sendstop(it);
	if(mo != nil){
		if((mo->o->f & Fresource) && (it->o->f & Fgather))
			sendgather(it, p, mo);
		else
			sendmovenear(it, p, mo);
	}else
		sendmove(it, p);
}

static void
drawhud(void)
{
	char s[256];
	Mobj *mo;

	draw(screen, Rpt(p0, screen->r.max), display->black, nil, ZP);
	mo = selected[0];
	if(mo == nil)
		return;
	if(mo->o->f & Fresource)
		snprint(s, sizeof s, "%s %d", mo->o->name, mo->amount);
	else
		snprint(s, sizeof s, "%s %d/%d", mo->o->name, mo->hp, mo->o->hp);
	string(screen, p0, display->white, ZP, font, s);
	if((mo->o->f & Fresource) == 0){
		snprint(s, sizeof s, "%s", mo->state < OSend ? statename[mo->state] : "");
		string(screen, addpt(p0, Pt(0,font->height)), display->white, ZP, font, s);
	}
}

static int
addvis(Mobj *mo)
{
	int i;

	if((i = nvis++) >= nvisbuf){
		visbuf = erealloc(visbuf, (nvisbuf + 16) * sizeof *visbuf,
			nvisbuf * sizeof *visbuf);
		nvisbuf += 16;
	}
	visbuf[i] = mo;
	return i;
}

static void
clearvis(void)
{
	if(visbuf != nil)
		memset(visbuf, 0, nvisbuf * sizeof *visbuf);
	nvis = 0;
}

static int
boundpic(Rectangle *r, u32int **q)
{
	int w;

	r->min.x -= pan.x / scale;
	r->min.y -= pan.y / scale;
	if(r->min.x + r->max.x < 0 || r->min.x >= fbw
	|| r->min.y + r->max.y < 0 || r->min.y >= fbh)
		return -1;
	w = r->max.x;
	if(r->min.x < 0){
		if(q != nil)
			*q -= r->min.x;
		r->max.x += r->min.x;
		r->min.x = 0;
	}
	if(r->min.x + r->max.x > fbw)
		r->max.x -= r->min.x + r->max.x - fbw;
	if(r->min.y < 0){
		if(q != nil)
			*q -= w * r->min.y;
		r->max.y += r->min.y;
		r->min.y = 0;
	}
	if(r->min.y + r->max.y > fbh)
		r->max.y -= r->min.y + r->max.y - fbh;
	r->min.x *= scale;
	r->max.x *= scale;
	return 0;
}

static void
drawpic(int x, int y, Pic *pic, int ivis)
{
	int n, Δp, Δsp, Δq;
	u32int v, *p, *e, *sp, *q;
	Rectangle r;

	if(pic->p == nil)
		sysfatal("drawpic: empty pic");
	q = pic->p;
	r = Rect(x + pic->dx, y + pic->dy, pic->w, pic->h);
	if(boundpic(&r, &q) < 0)
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
drawpicalpha(int x, int y, Pic *pic)
{
	int n, Δp, Δq;
	u8int k, a, b;
	u32int o, A, B, *p, *e, *q;
	Rectangle r;

	if(pic->p == nil)
		sysfatal("drawpic: empty pic");
	q = pic->p;
	r = Rect(x + pic->dx, y + pic->dy, pic->w, pic->h);
	if(boundpic(&r, &q) < 0)
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
				o = k * (a - b);
				o = (o + 1 + (o >> 8)) >> 8;
				B = B & ~(0xff << n) | (o + b & 0xff) << n;
			}
			for(n=0; n<scale; n++)
				*p++ = B;
		}
		q += Δq;
		p += Δp;
	}
}

void
compose(int x, int y, u32int c)
{
	int n, Δp;
	u32int v, *p, *e;
	Rectangle r;

	r = Rect(x * Nodewidth, y * Nodeheight, Nodewidth, Nodeheight);
	if(boundpic(&r, nil) < 0)
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

static void
drawmap(Rectangle r)
{
	int x, y;
	u64int *row, v, m;
	Node *n;
	Mobj *mo;
	Point *p;

	r = Rpt(mulpt(r.min, Node2Tile), mulpt(r.max, Node2Tile));
	for(y=r.min.y, n=nodemap+y*nodemapwidth+r.min.x; y<r.max.y; y++){
		x = r.min.x;
		row = baddr(x, y);
		v = *row++;
		m = 1ULL << 63 - (x & Bmask);
		for(; x<r.max.x; x++, n++, m>>=1){
			if(m == 0){
				v = *row++;
				m = 1ULL << 63;
			}
			if(v & m)
				compose(x, y, 0xff0000);
			if(n->closed)
				compose(x, y, 0x0000ff);
			else if(n->open)
				compose(x, y, 0xffff00);
		}
		n += nodemapwidth - (r.max.x - r.min.x);
	}
	if((mo = selected[0]) != nil && mo->pathp != nil){
		for(p=mo->paths; p<mo->pathe; p++)
			compose(p->x / Nodewidth, p->y / Nodeheight, 0x00ff00);
		compose(mo->target.x, mo->target.y, 0x00ffff);
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
	if(n == OSskymaybe)
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

	for(dl=drawlist; dl<drawlist+DLend; dl++)
		dl->n = 0;
}

static void
drawmobjs(void)
{
	int n;
	Mobj *mo;
	Drawlist *dl;

	for(dl=drawlist; dl<drawlist+DLend; dl++)
		for(n=0; n<dl->n; n++){
			mo = dl->mo[n];
			if(dl->noalpha)
				drawpic(mo->px, mo->py, dl->pics[n], addvis(mo));
			else
				drawpicalpha(mo->px, mo->py, dl->pics[n]);
		}
}

static void
addpic(Drawlist *dl, Mobj *mo, int type)
{
	int n;
	Pic *p;

	if((p = frm(mo, type)) == nil)
		return;
	if(dl->n >= dl->sz){
		n = dl->sz * sizeof *dl->pics;
		dl->pics = erealloc(dl->pics, n + 16 * sizeof *dl->pics, n);
		dl->mo = erealloc(dl->mo, n + 16 * sizeof *dl->mo, n);
		dl->sz += 16;
	}
	n = dl->n++;
	dl->pics[n] = p;
	dl->mo[n] = mo;
}

static void
addmobjs(Map *m)
{
	int air;
	Mobj *mo;
	Mobjl *ml;

	for(ml=m->ml.l; ml!=&m->ml; ml=ml->l){
		mo = ml->mo;
		air = mo->o->f & Fair;
		addpic(drawlist + (air ? DLairshad : DLgndshad), mo, PTshadow);
		addpic(drawlist + (air ? DLair : DLgnd), mo, PTbase);
		if(mo->state == OSmove)
			addpic(drawlist + (air ? DLairglow : DLgndglow), mo, PTglow);
	}
}

static Rectangle
setdrawrect(void)
{
	Rectangle r;

	r.min.x = pan.x / scale / Tilewidth;
	r.min.y = pan.y / scale / Tileheight;
	r.max.x = r.min.x + (pan.x / scale % Tilewidth != 0);
	r.max.x += fbw / Tilewidth + (fbw % Tilewidth != 0);
	if(r.max.x > mapwidth)
		r.max.x = mapwidth;
	r.max.y = r.min.y + (pan.y / scale % Tileheight != 0);
	r.max.y += fbh / Tileheight + (fbh % Tilewidth != 0);
	if(r.max.y > mapheight)
		r.max.y = mapheight;
	/* enlarge window to capture units overlapping multiple tiles;
	 * seems like the easiest way to take this into account */
	r.min.x = max(r.min.x - 4, 0);
	r.min.y = max(r.min.y - 4, 0);
	return r;
}

void
redraw(void)
{
	int x, y;
	Rectangle r;
	Map *m;

	clearvis();
	clearlists();
	r = setdrawrect();
	for(y=r.min.y, m=map+y*mapwidth+r.min.x; y<r.max.y; y++){
		for(x=r.min.x; x<r.max.x; x++, m++){
			drawpic(x*Tilewidth, y*Tileheight, m->t->p, -1);
			addmobjs(m);
		}
		m += mapwidth - (r.max.x - r.min.x);
	}
	drawmobjs();
	if(debugmap)
		drawmap(r);
	drawhud();
}

void
updatefb(void)
{
	qlock(&drawlock);
	redraw();
	qunlock(&drawlock);
	drawfb();
}

void
resetfb(void)
{
	fbws = min(nodemapwidth * Nodewidth * scale, Dx(screen->r));
	fbh = min(nodemapheight * Nodeheight * scale, Dy(screen->r));
	selr = Rpt(screen->r.min, addpt(screen->r.min, Pt(fbws, fbh)));
	p0 = Pt(screen->r.min.x + 8, screen->r.max.y - 3 * font->height);
	p0.y -= (p0.y - screen->r.min.y) % scale;
	panmax.x = max(Nodewidth * nodemapwidth * scale - Dx(screen->r), 0);
	panmax.y = max(Nodeheight * nodemapheight * scale - Dy(screen->r), 0);
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
drawfb(void)
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
