#include <u.h>
#include <libc.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

enum{
	Nmaxsize = 4*4,	/* FIXME: seems like a shitty assumption to make */
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
baddr(Point p)
{
	p.x >>= Bshift;
	p.x += Npad;
	p.y += Npad;
	return bmap + p.y * bmapwidth + p.x;
}

u64int *
rbaddr(Point p)
{
	p.x >>= Bshift;
	p.x += Npad;
	p.y += Npad;
	return rbmap + p.y * rbmapwidth + p.x;
}

static u64int *
breduce(u64int *b, int Δb, int ofs, Point sz, Point Δsz, int left)
{
	static u64int row[Nmaxsize+2];
	int i, j;
	u64int u, m;

	memset(row, 0xfe, sizeof row);
	m = (1 << sz.x - 1) - 1;
	if(left){
		ofs = 64 - sz.x - Δsz.x - ofs;
		m <<= 63 - sz.x + 1;
	}
	m = ~m;
	for(i=0; i<sz.y+Δsz.y; i++, b+=Δb){
		assert(i < nelem(row));
		u = b[0];
		if(ofs > 0){
			if(left){
				u >>= ofs;
				u |= b[-1] << 64 - ofs;
			}else{
				u <<= ofs;
				u |= b[1] >> 64 - ofs;
			}
		}
		if(left)
			switch(sz.x){
			case 4: u |= u >> 1 | u >> 2 | u >> 3; break;
			case 2: u |= u >> 1; break;
			}
		else
			switch(sz.x){
			case 4: u |= u << 1 | u << 2 | u << 3; break;
			case 2: u |= u << 1; break;
			}
		u &= m;
		row[i] = u;
		for(j=max(i-sz.y+1, 0); j<i; j++)
			row[j] |= u;
	}
	return row;
}

u64int *
bload(Point p, Point sz, Point Δsz, int left, int rot)
{
	int ofs, Δb;
	u64int *b;

	if(rot){
		b = rbaddr(p);
		Δb = rbmapwidth;
		ofs = p.y & Bmask;
	}else{
		b = baddr(p);
		Δb = bmapwidth;
		ofs = p.x & Bmask;
	}
	return breduce(b, Δb, ofs, sz, Δsz, left);
}

void
bset(Point p, Point sz, int set)
{
	int i, Δ, n;
	u64int *b, m, m´;

	b = baddr(p);
	n = p.x & Bmask;
	m = (1ULL << sz.x) - 1 << 64 - sz.x;
	m >>= n;
	Δ = n + sz.x - 64;
	m´ = (1ULL << Δ) - 1 << 64 - Δ;
	for(i=0; i<sz.y; i++, b+=bmapwidth){
		b[0] = set ? b[0] | m : b[0] & ~m;
		if(Δ > 0)
			b[1] = set ? b[1] | m´ : b[1] & ~m´;
	}
	b = rbaddr(p);
	n = p.y & Bmask;
	m = (1ULL << sz.y) - 1 << 64 - sz.y;
	m >>= n;
	Δ = n + sz.y - 64;
	m´ = (1ULL << Δ) - 1 << 64 - Δ;
	for(i=0; i<sz.x; i++, b+=rbmapwidth){
		b[0] = set ? b[0] | m : b[0] & ~m;
		if(Δ > 0)
			b[1] = set ? b[1] | m´ : b[1] & ~m´;
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

	bmapwidth = (mapwidth >> Bshift) + 2 * Npad;
	bmapheight = mapheight + 2 * Npad;
	rbmapwidth = (mapheight >> Bshift) + 2 * Npad;
	rbmapheight = mapwidth + 2 * Npad;
	bmap = emalloc(bmapwidth * bmapheight * sizeof *bmap);
	rbmap = emalloc(rbmapwidth * rbmapheight * sizeof *rbmap);
	for(i=0; i<Npad; i++){
		memset(bmap + i * mapwidth, 0xff, bmapwidth * sizeof *bmap);
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
