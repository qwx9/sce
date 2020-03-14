#include <u.h>
#include <libc.h>
#include <draw.h>
#include <memdraw.h>
#include <bio.h>

enum{
	Subsz = 8,
	Nmega = 4,
	Nsub = 4,
	WPErecord = 4,
	VR4record = Subsz * Subsz,
	VX4record = Nsub * Nsub * 2,
	CV5hdr = 20,
	CV5record = CV5hdr + Nmega * Nmega * 2,
	Rgbsz = 3,
	Tilewidth = Nsub * Subsz,
	Tileheight = Tilewidth,
	Tilesz = Tilewidth * Tileheight,
	Megawidth = Nmega * Tilewidth,
	Megaheight = Megawidth,
	Megasz = Megawidth * Megaheight,
};

uchar *pal, *data, *ref;
int npal, ndata, nref;

Biobuf *
bopen(char *pref, char *ext, vlong *size)
{
	int fd;
	char path[64];
	Dir *d;
	Biobuf *bf;

	snprint(path, sizeof path, "%s.%s", pref, ext);
	if((fd = open(path, OREAD)) < 0)
		sysfatal("open: %r");
	if((d = dirfstat(fd)) == nil)
		sysfatal("dirfstat: %r");
	*size = d->length;
	free(d);
	if((bf = Bfdopen(fd, OREAD)) == nil)
		sysfatal("Bfdopen: %r");
	return bf;	
}

void
readall(char *pref, char *ext, uchar **buf, int *nbuf, int elsize)
{
	int n;
	uchar *p;
	vlong size;
	Biobuf *bf;

	bf = bopen(pref, ext, &size);
	if(size % elsize != 0)
		sysfatal("readall %s.%s: invalid size", pref, ext);
	if((*buf = malloc(size)) == nil)
		sysfatal("malloc: %r");
	p = *buf;
	while((n = Bread(bf, p, 128 * elsize)) > 0)
		p += n;
	if(n < 0)
		sysfatal("Bread: %r");
	if(p - *buf != size)
		sysfatal("readall %s.%s: phase error (%zd vs %lld)", pref, ext, p-*buf, size);
	*nbuf = size / elsize;
	Bterm(bf);
}

void
gettile(int n, uchar *buf, int bufwidth)
{
	int i, x, tx, flip;
	uchar *rp, *re, *dp, *de, *pp, *p;

	for(rp=ref+n*VX4record, re=rp+VX4record, p=buf, tx=0; rp<re; rp+=2){
		i = (rp[1] << 8 | rp[0]) >> 1;
		if(i >= ndata)
			sysfatal("writetile: invalid tile index %ud", i);
		flip = rp[0] & 1;
		for(dp=data+i*VR4record, de=dp+VR4record; dp<de;){
			for(x=0; x<Subsz; x++, p+=Rgbsz){
				pp = pal + (flip ? dp[Subsz-1 - x] : dp[x]) * WPErecord;
				p[0] = pp[2];
				p[1] = pp[1];
				p[2] = pp[0];
			}
			dp += Subsz;
			p += (bufwidth - Subsz) * Rgbsz;
		}
		p -= (Subsz * bufwidth - Subsz) * Rgbsz;
		if(++tx % Nsub == 0)
			p += ((Subsz - 1) * bufwidth + (bufwidth - Tilewidth)) * Rgbsz;
	}
}

void
writetile(char *pref, int n, char *id, uchar *buf, int nbuf, Memimage *m)
{
	int fd;
	char path[128];

	if(loadmemimage(m, m->r, buf, nbuf) < 0)
		sysfatal("loadmemimage: %r");
	snprint(path, sizeof path, "%s.%s%05d.bit", pref, id, n);
	if((fd = create(path, OWRITE, 0644)) < 0)
		sysfatal("open: %r");
	if(writememimage(fd, m) < 0)
		sysfatal("writememimage: %r");
	close(fd);
}

void
parsecv5(char *pref)
{
	int n, nr, x, y;
	vlong size;
	uchar u[CV5record], *rp, buf[Megasz * Rgbsz], *p;
	Biobuf *bf;
	Memimage *m;

	bf = bopen(pref, "cv5", &size);
	if(size % CV5record != 0)
		sysfatal("parsecv5 %s: invalid size", pref);
	if((m = allocmemimage(Rect(0,0,Megawidth,Megaheight), RGB24)) == nil)
		sysfatal("allocmemimage: %r");
	nr = 0;
	while((n = Bread(bf, u, sizeof u)) > 0){
		assert(n == sizeof u);
		for(y=0, rp=u+CV5hdr, p=buf; y<Nmega; y++){
			for(x=0; x<Nmega; x++, rp+=2){
				gettile(rp[1] << 8 | rp[0], p, Megawidth);
				p += Tilewidth * Rgbsz;
			}
			p += (Tileheight - 1) * Megawidth * Rgbsz;
		}
		writetile(pref, nr++, "g", buf, Megasz * Rgbsz, m);
	}
	if(n < 0)
		sysfatal("Bread: %r");
	Bterm(bf);
}

void
parsetile(char *pref)
{
	int n;
	uchar buf[Tilesz * Rgbsz];
	Memimage *m;

	if((m = allocmemimage(Rect(0,0,Tilewidth,Tileheight), RGB24)) == nil)
		sysfatal("allocmemimage: %r");
	for(n=0; n<nref; n++){
		gettile(n, buf, Tilewidth);
		writetile(pref, n, "", buf, Tilesz * Rgbsz, m);
	}
}

void
usage(void)
{
	fprint(2, "usage: %s [-g] tileset\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	int group;

	group = 0;
	ARGBEGIN{
	case 'g': group = 1; break;
	default: usage();
	}ARGEND
	if(argv[0] == nil)
		usage();
	readall(argv[0], "wpe", &pal, &npal, WPErecord);
	if(npal != 256)
		sysfatal("invalid pal size %d", npal);
	readall(argv[0], "vr4", &data, &ndata, VR4record);
	readall(argv[0], "vx4", &ref, &nref, VX4record);
	if(memimageinit() < 0)
		sysfatal("memimageinit: %r");
	if(group)
		parsecv5(argv[0]);
	else
		parsetile(argv[0]);
	exits(nil);
}
