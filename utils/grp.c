#include <u.h>
#include <libc.h>
#include <draw.h>
#include <memdraw.h>
#include <bio.h>

typedef struct Hdr Hdr;
struct Hdr{
	u8int dx;
	u8int dy;
	u8int w;
	u8int h;
	u32int ofs;
};

int split, pcx, npal, idxonly;
Biobuf *bf;
uchar *bufp;
u32int pal[256], bgcol = 0xffff00;

void *
emalloc(ulong n)
{
	void *p;

	if((p = mallocz(n, 1)) == nil)
		sysfatal("emalloc: %r");
	setmalloctag(p, getcallerpc(&n));
	return p;
}

void
putcol(u32int c)
{
	if(pcx)
		*bufp++ = c >> 24;
	*bufp++ = c >> 16;
	*bufp++ = c >> 8;
	*bufp++ = c;
}

u8int
get8(void)
{
	uchar v;

	if(Bread(bf, &v, 1) != 1)
		sysfatal("get8: short read");
	return v;
}

u16int
get16(void)
{
	u8int v;

	v = get8();
	return get8() << 8 | v;
}

u32int
get32(void)
{
	u16int v;

	v = get16();
	return get16() << 16 | v;
}

void
getpcxpal(char *f)
{
	int i, n, a, fd;
	uchar *buf, *bp;
	u32int v, *p;
	Memimage *im, *im1;

	if((fd = open(f, OREAD)) < 0)
		sysfatal("getpcxpal: %r");
	if(memimageinit() < 0)
		sysfatal("memimageinit: %r");
	if((im = readmemimage(fd)) == nil)
		sysfatal("readmemimage: %r");
	close(fd);
	if(im->chan != RGB24){
		if((im1 = allocmemimage(im->r, RGB24)) == nil)
			sysfatal("allocmemimage: %r");
		memfillcolor(im1, DBlack);
		memimagedraw(im1, im1->r, im, im->r.min, memopaque, ZP, S);
		freememimage(im);
		im = im1;
	}
	if(Dx(im->r) != 256)
		sysfatal("invalid pcx palette: %d columns", Dx(im->r));
	n = Dx(im->r) * Dy(im->r);
	npal = Dy(im->r);
	buf = emalloc(n * sizeof *pal);
	if(unloadmemimage(im, im->r, buf, n * sizeof *pal) < 0)
		sysfatal("unloadmemimage: %r");
	freememimage(im);
	/* FIXME */
	//for(i=0, p=pal, bp=buf; i<npal; i++, p++, bp+=256*3){
	for(i=0, p=pal, bp=buf+20*3; i<npal; i++, p++, bp+=256*3){
		v = bp[0] << 24 | bp[1] << 16 | bp[2] << 8;
		a = 0x7f;
		switch(npal){
		case 63:
			if(i > 47)
				a = 0xff / (1 + exp(-i + 48 - 3.4) / 0.75);
			/* logistic growth function
			 * max / (1 + exp(-x + xofs) / slope) + yofs
			 * fplot -r '0:47 0:255' '255 / (1 + exp((20 - x) / 4)) + 0'
			 */
			else
				a = (0xff + 1) / (1 + exp((16 - i) / 3.5)) + 0;
			break;
		/* FIXME */
		case 40: a = i < 33 ? 0xff * i / 32 : 0xff * (6 - (i - 33)) / 6; break;
		case 32: a = 0xff * i / 30; a = a > 0xff ? 0xff : a; break;
		case 1: break;
		default: sysfatal("unknown palette size %d", npal);
		}
		*p = v | a;
	}
	free(buf);
}

void
getpal(char *f)
{
	uchar u[3];
	u32int *p;
	Biobuf *bp;

	if((bp = Bopen(f, OREAD)) == nil)
		sysfatal("getpal: %r");
	for(p=pal; p<pal+nelem(pal); p++){
		if(Bread(bp, u, 3) != 3)
			sysfatal("getpal: short read: %r");
		*p = u[2]<<16 | u[1]<<8 | u[0];
	}
	Bterm(bp);
}

void
usage(void)
{
	fprint(2, "usage: %s [-csx] [-b bgcol] pal pic\n", argv0);
	exits("usage");
}

/* unpack a GRP file containing sprites.
 * palette may be a plain file with 256 RGB24 entries (3*256 bytes)
 * or a decoded PCX image serving as a palette.
 * in that last case, palette must be provided in image(6) format,
 * and palette indexes in the grp reference a column (palette) in
 * the PCX image, which is used to implement transparency.
 * we use the first column to set color and an alpha value for
 * compositing, rather than use remapping with the PCX palette.
 */
void
main(int argc, char **argv)
{
	int fd, n, x, y;
	char *s, c[9];
	u8int i;
	u16int ni, maxx, maxy, *ofs;
	uchar *buf;
	Hdr *h, *hp;
	Biobuf *bo;

	ARGBEGIN{
	case 'b': bgcol = strtoul(EARGF(usage()), nil, 0); break;
	case 'c': idxonly = 1; break;
	case 's': split = 1; break;
	case 'x': pcx = 1; break;
	default: usage();
	}ARGEND
	if(argv[0] == nil || argv[1] == nil)
		usage();
	if(pcx)
		getpcxpal(argv[0]);
	else
		getpal(argv[0]);
	if((fd = open(argv[1], OREAD)) < 0)
		sysfatal("open: %r");
	if((bf = Bfdopen(fd, OREAD)) == nil)
		sysfatal("Bfdopen: %r");
	ni = get16();
	maxx = get16();
	maxy = get16();
	bo = nil;
	h = emalloc(ni * sizeof *h);
	buf = emalloc(maxx * maxy * sizeof(u32int) * (split ? 1 : ni));
	ofs = emalloc(maxy * sizeof *ofs);
	s = emalloc(strlen(argv[1]) + strlen(".00000.bit"));
	for(hp=h; hp<h+ni; hp++){
		hp->dx = get8();
		hp->dy = get8();
		hp->w = get8();
		hp->h = get8();
		hp->ofs = get32();
	}
	bufp = buf;
	if(!split && (bo = Bfdopen(1, OWRITE)) == nil)
		sysfatal("Bfdopen: %r");
	chantostr(c, pcx ? ARGB32 : RGB24);
	for(hp=h; hp<h+ni; hp++){
		if(split){
			sprint(s, "%s.%05zd.bit", argv[1], hp-h);
			if((bo = Bopen(s, OWRITE)) == nil)
				sysfatal("Bfdopen: %r");
			Bprint(bo, "%11s %11d %11d %11d %11d ", c,
				hp->dx, hp->dy, hp->dx+hp->w, hp->dy+hp->h);
		}
		Bseek(bf, hp->ofs, 0);
		for(y=0; y<hp->h; y++)
			ofs[y] = get16();
		if(!split){
			for(y=0; y<hp->dy; y++)
				for(x=0; x<maxx; x++)
					putcol(bgcol);
		}
		for(y=0; y<hp->h; y++){
			if(!split)
				for(x=0; x<hp->dx; x++)
					putcol(bgcol);
			Bseek(bf, hp->ofs + ofs[y], 0);
			x = 0;
			while(x < hp->w){
				i = get8();
				n = i & 0x7f;
				if(i & 1<<7){
					x += n;
					while(n-- > 0)
						putcol(bgcol);
				}else if(i & 1<<6){
					n &= 0x3f;
					x += n;
					i = get8();
					if(pcx && --i >= npal)
						sysfatal("invalid pal index %d", i);
					while(n-- > 0){
						if(!idxonly)
							putcol(pal[i]);
						else
							putcol(0xff0000 | i);
					}
				}else{
					x += i;
					while(n-- > 0){
						i = get8();
						if(pcx && --i >= npal)
							sysfatal("invalid pal index %d", i);
						if(!idxonly)
							putcol(pal[i]);
						else
							putcol(0xff0000 | i);
					}
				}
			}
			if(!split)
				for(; x<maxx-hp->dx; x++)
					putcol(bgcol);
		}
		if(!split)
			for(y+=hp->dy; y<maxy; y++)
				for(x=0; x<maxx; x++)
					putcol(bgcol);
		else{
			Bwrite(bo, buf, bufp - buf);
			Bterm(bo);
			bufp = buf;
		}
	}
	if(!split){
		Bprint(bo, "%11s %11d %11d %11d %11d ", c, 0, 0, maxx, maxy * ni);
		Bwrite(bo, buf, bufp - buf);
		Bterm(bo);
	}
	exits(nil);
}
