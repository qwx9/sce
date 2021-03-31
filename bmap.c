#include <u.h>
#include <libc.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

enum{
	Nmaxsize = 4,
	Npad = 1,
};

static u64int *bmap, *rbmap;
static int bmapwidth, bmapheight, rbmapwidth, rbmapheight;

static uchar ffstab[256];

int
lsb(uvlong v)
{
	int c;

	c = 0;
	if((v & 0xffffffff) == 0){
		v >>= 32;
		c += 32;
	}
	if((v & 0xffff) == 0){
		v >>= 16;
		c += 16;
	}
	if((v & 0xff) == 0){
		v >>= 8;
		c += 8;
	}
	if((v & 0xf) == 0){
		v >>= 4;
		c += 4;
	}
	if((v & 3) == 0){
		v >>= 2;
		c += 2;
	}
	if((v & 1) == 0)
		c++;
	return c;
}

int
msb(uvlong v)
{
	int n;

	if(n = v >> 56)
		return 56 + ffstab[n];
	else if(n = v >> 48)
		return 48 + ffstab[n];
	else if(n = v >> 40)
		return 40 + ffstab[n];
	else if(n = v >> 32)
		return 32 + ffstab[n];
	else if(n = v >> 24)
		return 24 + ffstab[n];
	else if(n = v >> 16)
		return 16 + ffstab[n];
	else if(n = v >> 8)
		return 8 + ffstab[n];
	else
		return ffstab[v];
}

u64int *
baddr(int x, int y)
{
	x >>= Bshift;
	x += Npad;
	y += Npad;
	return bmap + y * bmapwidth + x;
}

u64int *
rbaddr(int y, int x)
{
	x >>= Bshift;
	x += Npad;
	y += Npad;
	return rbmap + y * rbmapwidth + x;
}

static u64int *
breduce(u64int *p, int Δp, int ofs, int w, int h, int Δw, int Δh, int left)
{
	static u64int row[Nmaxsize+2];
	int i, j;
	u64int u, m;

	m = (1 << w - 1) - 1;
	if(left){
		ofs = 64 - w - Δw - ofs;
		m <<= 63 - w + 1;
	}
	m = ~m;
	for(i=0; i<h+Δh; i++, p+=Δp){
		u = p[0];
		if(ofs > 0){
			if(left){
				u >>= ofs;
				u |= p[-1] << 64 - ofs;
			}else{
				u <<= ofs;
				u |= p[1] >> 64 - ofs;
			}
		}
		if(left)
			switch(w){
			case 4: u |= u >> 1 | u >> 2 | u >> 3; break;
			case 2: u |= u >> 1; break;
			}
		else
			switch(w){
			case 4: u |= u << 1 | u << 2 | u << 3; break;
			case 2: u |= u << 1; break;
			}
		u &= m;
		row[i] = u;
		for(j=max(i-h+1, 0); j<i; j++)
			row[j] |= u;
	}
	return row;
}

u64int *
bload(int x, int y, int w, int h, int Δw, int Δh, int left, int rot)
{
	int ofs, Δp;
	u64int *p;

	if(rot){
		p = rbaddr(x, y);
		Δp = rbmapwidth;
		ofs = y & Bmask;
	}else{
		p = baddr(x, y);
		Δp = bmapwidth;
		ofs = x & Bmask;
	}
	return breduce(p, Δp, ofs, w, h, Δw, Δh, left);
}

void
bset(int x, int y, int w, int h, int set)
{
	int i, Δ, n;
	u64int *p, m, m´;

	p = baddr(x, y);
	n = x & Bmask;
	m = (1ULL << w) - 1 << 64 - w;
	m >>= n;
	Δ = n + w - 64;
	m´ = (1ULL << Δ) - 1 << 64 - Δ;
	for(i=0; i<h; i++, p+=bmapwidth){
		p[0] = set ? p[0] | m : p[0] & ~m;
		if(Δ > 0)
			p[1] = set ? p[1] | m´ : p[1] & ~m´;
	}
	p = rbaddr(x, y);
	n = y & Bmask;
	m = (1ULL << h) - 1 << 64 - h;
	m >>= n;
	Δ = n + h - 64;
	m´ = (1ULL << Δ) - 1 << 64 - Δ;
	for(i=0; i<w; i++, p+=rbmapwidth){
		p[0] = set ? p[0] | m : p[0] & ~m;
		if(Δ > 0)
			p[1] = set ? p[1] | m´ : p[1] & ~m´;
	}
}

static void
initffs(void)
{
	int i;

	ffstab[0] = 0;
	ffstab[1] = 0;
	for(i=2; i<nelem(ffstab); i++)
		ffstab[i] = 1 + ffstab[i/2];
}

void
initbmap(void)
{
	int i;

	bmapwidth = (nodemapwidth >> Bshift) + 2 * Npad;
	bmapheight = nodemapheight + 2 * Npad;
	rbmapwidth = (nodemapheight >> Bshift) + 2 * Npad;
	rbmapheight = nodemapwidth + 2 * Npad;
	bmap = emalloc(bmapwidth * bmapheight * sizeof *bmap);
	rbmap = emalloc(rbmapwidth * rbmapheight * sizeof *rbmap);
	for(i=0; i<Npad; i++){
		memset(bmap + i * nodemapwidth, 0xff, bmapwidth * sizeof *bmap);
		memset(bmap + (bmapheight - i - 1) * bmapwidth, 0xff,
			bmapwidth * sizeof *bmap);
		memset(rbmap + i * rbmapwidth, 0xff, rbmapwidth * sizeof *rbmap);
		memset(rbmap + (rbmapheight - i - 1) * rbmapwidth, 0xff,
			rbmapwidth * sizeof *rbmap);
	}
	for(i=Npad; i<bmapheight-Npad; i++){
		memset(bmap + i * bmapwidth, 0xff, Npad * sizeof *bmap);
		memset(bmap + (i+1) * bmapwidth - Npad, 0xff, Npad * sizeof *bmap);
		memset(rbmap + i * rbmapwidth, 0xff, Npad * sizeof *rbmap);
		memset(rbmap + (i+1) * rbmapwidth - Npad, 0xff, Npad * sizeof *rbmap);
	}
	initffs();
}
