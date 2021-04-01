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

typedef struct Drawlist Drawlist;
struct Drawlist {
	Mobj **shad;
	Mobj **mo;
	Mobj **glow;
	int n;
	int glown;
	int sz;
};
static Drawlist gndlist, airlist;

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
select(Point p)
{
	int i;

	if(!ptinrect(p, selr))
		return;
	p = divpt(subpt(p, selr.min), scale);
	i = fbvis[p.y * fbw + p.x];
	selected[0] = i == -1 ? nil : visbuf[i];
}

void
move(Point p)
{
	int i;
	Point vp;
	Mobj *mo;

	if(!ptinrect(p, selr) || selected[0] == nil)
		return;
	vp = divpt(subpt(p, selr.min), scale);
	i = fbvis[vp.y * fbw + vp.x];
	mo = i == -1 ? nil : visbuf[i];
	if(mo == selected[0]){
		dprint("select: %#p not moving to itself\n", visbuf[i]);
		return;
	}
	p = divpt(addpt(subpt(p, selr.min), pan), scale);
	p.x /= Nodewidth;
	p.y /= Nodeheight;
	moveone(p, selected[0], mo);
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
	snprint(s, sizeof s, "%s %d/%d", mo->o->name, mo->hp, mo->o->hp);
	string(screen, p0, display->white, ZP, font, s);
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
	int θ, frm;
	Pics *pp;
	Pic *p;

	pp = &mo->o->pics[mo->state][type];
	assert(pp->pic != nil);
	frm = pp->iscopy ? mo->freezefrm : tc % pp->nf;
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
	gndlist.n = gndlist.glown = 0;
	airlist.n = airlist.glown = 0;
}

static void
drawmobjs(Drawlist *dl)
{
	int i;
	Mobj *mo;

	for(i=0; i<dl->n; i++){
		mo = dl->shad[i];
		drawpicalpha(mo->px, mo->py, frm(mo, PTshadow));
	}
	for(i=0; i<dl->n; i++){
		mo = dl->mo[i];
		drawpic(mo->px, mo->py, frm(mo, PTbase), addvis(mo));
	}
	for(i=0; i<dl->glown; i++){
		mo = dl->glow[i];
		drawpicalpha(mo->px, mo->py, frm(mo, PTglow));
	}
}

static void
addmobjs(Map *m)
{
	int n;
	Mobj *mo;
	Mobjl *ml;
	Drawlist *dl;

	for(ml=m->ml.l; ml!=&m->ml; ml=ml->l){
		mo = ml->mo;
		dl = mo->o->f & Fair ? &airlist : &gndlist;
		if(dl->n >= dl->sz){
			n = dl->sz * sizeof *dl->mo;
			dl->shad = erealloc(dl->shad, n + 16 * sizeof *dl->mo, n);
			dl->mo = erealloc(dl->mo, n + 16 * sizeof *dl->mo, n);
			dl->glow = erealloc(dl->glow, n + 16 * sizeof *dl->mo, n);
			dl->sz += 16;
		}
		n = dl->n++;
		dl->shad[n] = mo;
		dl->mo[n] = mo;
		if(mo->state == OSmove
		&& mo->o->pics[OSmove][PTglow].pic != nil)
			dl->glow[dl->glown++] = mo;
	}
}

static Rectangle
setdrawrect(void)
{
	int f;
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
	drawmobjs(&gndlist);
	drawmobjs(&airlist);
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
