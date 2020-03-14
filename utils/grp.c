#include <u.h>
#include <libc.h>
#include <draw.h>
#include <bio.h>

typedef struct Hdr Hdr;
struct Hdr{
	u8int dx;
	u8int dy;
	u8int w;
	u8int h;
	u32int ofs;
};

int split;
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
	fprint(2, "usage: %s [-s] [-b bgcol] pal pic\n", argv0);
	exits("usage");
}

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
	case 's': split = 1; break;
	default: usage();
	}ARGEND
	if(argv[0] == nil || argv[1] == nil)
		usage();
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
	buf = emalloc(maxx * maxy * 3 * (split ? 1 : ni));
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
	for(hp=h; hp<h+ni; hp++){
		if(split){
			sprint(s, "%s.%05zd.bit", argv[1], hp-h);
			if((bo = Bopen(s, OWRITE)) == nil)
				sysfatal("Bfdopen: %r");
			Bprint(bo, "%11s %11d %11d %11d %11d ", chantostr(c, RGB24),
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
					while(n-- > 0)
						putcol(pal[i]);
				}else{
					x += i;
					while(n-- > 0)
						putcol(pal[get8()]);
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
		Bprint(bo, "%11s %11d %11d %11d %11d ", chantostr(c, RGB24), 0, 0, maxx, maxy * ni);
		Bwrite(bo, buf, bufp - buf);
		Bterm(bo);
	}
	exits(nil);
}
