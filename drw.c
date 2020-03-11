#include <u.h>
#include <libc.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

int scale;
Point p0, pan;

static int fbsz, fbh, fbw;
static u32int *fb, *fbe;
static Image *fbi;
static Rectangle selr;
static Point panmax;
static Mobj *selected[Nselect];

/* FIXME: rescale -> pan might be wrong and display bullshit until we repan */

static int
max(int a, int b)
{
	return a > b ? a : b;
}

static int
min(int a, int b)
{
	return a < b ? a : b;
}

void
dopan(int dx, int dy)
{
	pan.x -= dx;
	pan.y -= dy;
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
mapselect(Point *pp)
{
	Path *p;

	p = path + pathwidth * (pp->y / Tlsubheight) + pp->x / Tlsubwidth;
	return p->lo.lo->mo;
}

void
select(Point p, int b)
{
	if(!ptinrect(p, selr))
		return;
	p = divpt(addpt(subpt(p, selr.min), pan), scale);
	/* FIXME: multiple selections */
	/* FIXME: selection map based on sprite dimensions and offset */
	if(b & 1)
		selected[0] = mapselect(&p);
	else if(b & 4){
		if(selected[0] == nil)
			return;
		/* FIXME: this implements move for any unit of any team,
		 * including buildings, but not attack or anything else */
		/* FIXME: attack, attackobj, moveobj (follow obj), etc. */
		/* FIXME: offset sprite size */
		//move(p.x & ~Tlsubmask, p.y & ~Tlsubmask, selected);
		move(p.x, p.y, selected);
	}
}

static void
drawpic(int x, int y, Pic *pic)
{
	int n, k, w, pw, s, off;
	u32int v, *pp, *ip;

	/* FIXME: naming here sucks and is very confusing */
	off = 0;
	ip = pic->p;
	pw = pic->w;
	n = pic->h;
	x -= pic->dx;
	y -= pic->dy;
	x -= pan.x / scale;
	y -= pan.y / scale;
	x *= scale;
	if(x + pw * scale < 0 || x > fbw
	|| y + n < 0 || y > fbh)
		return;
	if(y < 0){
		ip -= y * pw;
		n += y;
		y = 0;
	}
	if(x < 0){
		ip -= x / scale;
		off += pw;
		pw += x / scale;
		off -= pw;
		x = 0;
	}
	pp = fb + y * fbw + x;
	if(pp >= fbe)
		return;
	if(x + pw * scale > fbw){
		off += pw;
		pw = (fbw - x) / scale;
		off -= pw;
	}
	if(y + n > fbh)
		n = fbh - y;
	w = fbw - pw * scale;
	while(n-- > 0){
		k = pw;
		while(k-- > 0){
			v = *ip++;
			if(v & 0xff << 24){
				for(s=0; s<scale; s++)
					*pp++ = v;
			}else
				pp += scale;
		}
		ip += off;
		pp += w;
	}
}

static void
compose(Path *p, u32int c)
{
	int n, k, x, w;
	u32int v, *pp;

	pp = fb + p->y * Tlsubheight * fbw + p->x * Tlsubwidth * scale;
	n = Tlsubheight;
	w = fbw - Tlsubwidth * scale;
	while(n-- > 0){
		x = Tlsubwidth;
		if(pp + x >= fbe)
			return;
		if(pp >= fb){
			while(x-- > 0){
				v = *pp;
				v = (v & 0xff0000) + (c & 0xff0000) >> 1 & 0xff0000
					| (v & 0xff00) + (c & 0xff00) >> 1 & 0xff00
					| (v & 0xff) + (c & 0xff) >> 1 & 0xff;
				k = scale;
				while(k-- > 0)
					if(pp >= fb && pp < fbe)
						*pp++ = v;
			}
		}else
			pp += x * scale;
		pp += w;
	}
}

static Pic *
frm(Mobj *mo)
{
	Pics *p;

	p = mo->pics;
	return p->p + p->n * (mo->team-1) + p->nf * mo->θ + tc % p->nf;
}

void
redraw(void)
{
	char s[256];
	Point p;
	Map *m;
	Mobj *mo;
	Lobj *lo;
	Path *pp;

	/* FIXME: only process visible parts of the screen and adjacent tiles */
	for(m=map; m<map+mapwidth*mapheight; m++)
		drawpic(m->x, m->y, &m->t->pic);
	/* FIXME: draw overlay (creep) */
	/* FIXME: draw corpses */
	for(m=map; m<map+mapwidth*mapheight; m++)
		for(lo=m->lo.lo; lo!=&m->lo; lo=lo->lo){
			mo = lo->mo;
			drawpic(mo->p.x, mo->p.y, frm(mo));
		}
	for(pp=path; pp<path+pathwidth*pathheight; pp++)
		if(pp->blk != nil)
			compose(pp, 0xff00ff);
	/* FIXME: draw hud */
	draw(screen, Rpt(p0, screen->r.max), display->black, nil, ZP);
	mo = selected[0];
	if(mo == nil)
		return;
	/* FIXME: selected should be its own layer, not mapped to Map,
	 * since the coordinates won't match */
	p = p0;
	snprint(s, sizeof s, "%s %d/%d", mo->o->name, mo->hp, mo->o->hp);
	string(screen, p, display->white, ZP, font, s);
}

void
resetfb(void)
{
	if(scale < 1)
		scale = 1;
	else if(scale > 16)
		scale = 16;
	fbw = min(mapwidth * Tlwidth * scale, Dx(screen->r));
	fbh = min(mapheight * Tlheight * scale, Dy(screen->r));
	selr = Rpt(screen->r.min, addpt(screen->r.min, Pt(fbw, fbh)));
	p0 = Pt(screen->r.min.x + 8, screen->r.max.y - 3 * font->height);
	panmax.x = max(Tlwidth * mapwidth * scale - Dx(screen->r), 0);
	panmax.y = max(Tlheight * mapheight * scale - Dy(screen->r), 0);
	if(p0.y < selr.max.y){
		panmax.y += (selr.max.y - p0.y) * scale;
		selr.max.y = p0.y;
	}
	fbh /= scale;
	fbsz = fbw * fbh * sizeof *fb;
	free(fb);
	freeimage(fbi);
	fb = emalloc(fbsz);
	fbe = fb + fbsz / sizeof *fb;
	if((fbi = allocimage(display,
		Rect(0,0,fbw,scale==1 ? fbh : 1),
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
	if(r.max.y > p0.y)
		r.max.y = p0.y;
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
