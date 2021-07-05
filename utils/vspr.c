#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <keyboard.h>
#include <mouse.h>

enum{
	Nodesz = 32,
	Nspr = 8,
	Nrot = 17,
	Ndt = 50,
};

Keyboardctl *kc;
Mousectl *mc;
Channel *tmc;
int pause;
QLock pauselck;
int Δt = 1000;
Point pan, center, shadofs;
int frm, nfrm, nshfrm, rot, nrot, nspr, cansz;
char *name;
Image *canvas, *gridcol, *selcol, *bgcol, **imtab, **shtab;

int rot17[Nrot] = {
	0, 2, 4, 6, 8, 10, 12, 14, 16, 17, 19, 21, 23, 25, 27, 29, 31
};

void
resetdraw(void)
{
	freeimage(canvas);
	if((canvas = allocimage(display, Rect(0,0,Dx(screen->r),Dy(screen->r)), screen->chan, 0, DNofill)) == nil)
		sysfatal("allocimage: %r");
	cansz = canvas->r.max.x > canvas->r.max.y ? canvas->r.max.x : canvas->r.max.y;
	center = (Point){Nodesz * (Dx(canvas->r)/Nodesz / 2), Nodesz * (Dy(canvas->r)/Nodesz / 2)};
}

void
redraw(void)
{
	int n;
	char s[128];
	Image *ui, *us;
	Point p, o;
	Rectangle r;

	o = addpt(center, pan);
	draw(canvas, canvas->r, bgcol, nil, ZP);
	ui = imtab[nrot * frm + rot];
	us = nil;
	if(shtab != nil){
		us = shtab[nrot * (nshfrm != 1 ? frm : 0) + rot];
		draw(canvas, rectaddpt(us->r, addpt(o, shadofs)), us, us, us->r.min);
	}
	r = ui->r;
	r.max.y = r.min.y + Dy(r) / nspr;
	draw(canvas, rectaddpt(r, o), ui, ui, ui->r.min);
	for(n=Nodesz; n<cansz; n+=Nodesz){
		line(canvas, Pt(n,0), Pt(n,canvas->r.max.y), 0, 0, 0, gridcol, ZP);
		line(canvas, Pt(0,n), Pt(canvas->r.max.x,n), 0, 0, 0, gridcol, ZP);
	}
	p = addpt(ui->r.min, o);
	line(canvas, p, Pt(p.x+Nodesz, p.y), 0, 0, 0, selcol, ZP);
	line(canvas, p, Pt(p.x, p.y+Nodesz), 0, 0, 0, selcol, ZP);
	line(canvas, Pt(p.x, p.y+Nodesz), Pt(p.x+Nodesz+1, p.y+Nodesz), 0, 0, 0, selcol, ZP);
	line(canvas, Pt(p.x+Nodesz, p.y), Pt(p.x+Nodesz, p.y+Nodesz), 0, 0, 0, selcol, ZP);
	if(us != nil)
		snprint(s, sizeof s, "%s frm %02d rot %02d size %R sha %R", name, frm, nrot==Nrot ? rot17[rot] : rot, r, us->r);
	else
		snprint(s, sizeof s, "%s frm %02d rot %02d size %R", name, frm, nrot==Nrot ? rot17[rot] : rot, r);
	string(canvas, Pt(8,0), gridcol, ZP, font, s);
	snprint(s, sizeof s, "ofs %P shofs %P Δt %d", pan, shadofs, Δt);
	string(canvas, Pt(8,font->height), gridcol, ZP, font, s);
	draw(screen, screen->r, canvas, nil, ZP);
	flushimage(display, 1);
}

Image *
loadframe(char *frm, int rot, int shad)
{
	int n, fd;
	char *path;
	uchar *buf, *p;
	Image *i, *im;

	if((path = smprint("/sys/games/lib/sce/%s.%s.%02ud%s.bit", name, frm, rot, shad?".s":"")) == nil)
		sysfatal("mprint: %r");
	if((fd = open(path, OREAD)) < 0){
		fprint(2, "open: %r\n");
		return nil;
	}
	if((i = readimage(display, fd, 0)) == nil)
		sysfatal("readimage: %r");
	close(fd);
	if(i->depth == 32)
		goto end;
	im = i;
	if((i = allocimage(display, im->r, ARGB32, 0, DTransparent)) == nil)
		sysfatal("allocimage: %r");
	draw(i, i->r, im, nil, i->r.min);
	freeimage(im);
	n = Dy(i->r) * bytesperline(i->r, i->depth) * 2;
	if((buf = malloc(n)) == nil)
		sysfatal("malloc: %r");
	unloadimage(i, i->r, buf, n);
	for(p=buf; p<buf+n; p+=4)
		if((p[2] << 16 | p[1] << 8 | p[0]) == 0x00ffff)
			p[3] = 0;
	loadimage(i, i->r, buf, n);
	free(buf);
end:
	free(path);
	return i;
}

void
setpause(void)
{
	if(pause ^= 1)
		qlock(&pauselck);
	else
		qunlock(&pauselck);
}

void
timeproc(void *)
{
	for(;;){
		qlock(&pauselck);
		nbsendul(tmc, 0);
		sleep(Δt);
		qunlock(&pauselck);
	}
}

void
usage(void)
{
	fprint(2, "usage: %s [-crs] name frame..\n", argv0);
	threadexits("usage");
}

void
threadmain(int argc, char **argv)
{
	int n, nr, shad;
	Image **i, **s;
	Mouse m;
	Rune r;

	shad = 1;
	nrot = Nrot;
	nspr = Nspr;
	ARGBEGIN{
	case 'c': nspr = 1; break;
	case 'r': nrot = 1; break;
	case 's': shad = 0; break;
	default: usage();
	}ARGEND
	if(argc < 2)
		usage();
	if(initdraw(nil, nil, "vspr") < 0)
		sysfatal("initdraw: %r");
	name = *argv++;
	argc--;
	nfrm = argc;
	nshfrm = nfrm;
	if((imtab = malloc(nfrm * nrot * sizeof *imtab)) == nil)
		sysfatal("malloc: %r");
	if(shad && (shtab = malloc(nshfrm * nrot * sizeof *shtab)) == nil)
		sysfatal("malloc: %r");
	i = imtab;
	s = shtab;
	while(*argv != nil){
		for(n=0; n<nrot; n++){
			nr = nrot == Nrot ? rot17[n] : n;
			if((*i++ = loadframe(*argv, nr, 0)) == nil)
				sysfatal("missing frame");
			if(shad){
				if((*s++ = loadframe(*argv, nr, 1)) == nil){
					if(s - shtab == 1 || n != 0)
						sysfatal("missing frame");
					nshfrm = 1;
				}
			}
		}
		argv++;
	}
	if(nshfrm == 1 && nfrm > 1)
		fprint(2, "only one shadow frame will be used\n");
	fmtinstall('P', Pfmt);
	fmtinstall('R', Rfmt);
	resetdraw();
	gridcol = display->black;
	if((bgcol = allocimage(display, Rect(0,0,1,1), screen->chan, 1, 0x00ffffff)) == nil
	|| (selcol = allocimage(display, Rect(0,0,1,1), screen->chan, 1, DGreen)) == nil)
		sysfatal("allocimage: %r");
	redraw();
	if((kc = initkeyboard(nil)) == nil)
		sysfatal("initkeyboard: %r");
	if((mc = initmouse(nil, screen)) == nil)
		sysfatal("initmouse: %r");
	m.xy = ZP;
	if((tmc = chancreate(sizeof(ulong), 0)) == nil)
		sysfatal("chancreate: %r");
	if(proccreate(timeproc, nil, 8192) < 0)
		sysfatal("proccreate: %r");
	Alt a[] = {
		{mc->resizec, nil, CHANRCV},
		{mc->c, &mc->Mouse, CHANRCV},
		{kc->c, &r, CHANRCV},
		{tmc, nil, CHANRCV},
		{nil, nil, CHANEND}
	};
	for(;;){
		switch(alt(a)){
		case 0:
			if(getwindow(display, Refnone) < 0)
				sysfatal("getwindow: %r");
			m = mc->Mouse;
			resetdraw();
			redraw();
			break;
		case 1:
			if(eqpt(m.xy, ZP))
				m = mc->Mouse;
			if((mc->buttons & 1) == 1 && !eqpt(m.xy, ZP)){
				pan.x += mc->xy.x - m.xy.x;
				pan.y += mc->xy.y - m.xy.y;
				redraw();
			}
			m = mc->Mouse;
			break;
		case 2:
			switch(r){
			case '0': case '1': case '2': case '3': case '4':
			case '5': case '6': case '7': case '8': case '9':
				r -= '0';
				if(r < nfrm){
					if(!pause)
						setpause();
					frm = r;
					redraw();
				}
				break;
			case '=':
			case '+': Δt += Ndt; redraw(); break;
			case '-': if(Δt > Ndt){ Δt -= Ndt; redraw(); } break;
			case 'r':
			case Kesc: pan = ZP; redraw(); break;
			case ' ': setpause(); break;
			case 'w': shadofs.y--; redraw(); break;
			case 's': shadofs.y++; redraw(); break;
			case 'a': shadofs.x--; redraw(); break;
			case 'd': shadofs.x++; redraw(); break;
			case Kup: pan.y--; redraw(); break;
			case Kdown: pan.y++; redraw(); break;
			case Kright: rot = (rot + 1) % nrot; redraw(); break;
			case Kleft: if(--rot < 0) rot = nrot - 1; redraw(); break;
			case Kdel: case 'q': threadexitsall(nil);
			}
			break;
		case 3:
			frm = (frm + 1) % nfrm;
			redraw();
			break;
		default:
			threadexitsall("alt: %r");
		}
	}
}
