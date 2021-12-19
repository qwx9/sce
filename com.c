#include <u.h>
#include <libc.h>
#include <draw.h>
#include <fcall.h>
#include "dat.h"
#include "fns.h"

enum{
	Hdrsz = 2,
};
typedef struct Header Header;
struct Header{
	ushort size;
};

#define	MGATHER		"dl dl"
#define	MMOVENEAR	"dl dl"
#define MMOVE		"dl dd"
#define MSTOP		"dl"

static int
vunpack(uchar *p, uchar *e, char *fmt, va_list a)
{
	int n, sz;

	sz = 0;
	for(;;){
		n = 0;
		switch(*fmt++){
		default: sysfatal("vunpack: unknown format %c", fmt[-1]);
		error: werrstr("vunpack: truncated message"); return -1;
		case 0: return sz;
		case ' ': break;
		case 'h': n = sizeof(u8int); if(p + n > e) goto error;
			*va_arg(a, int*) = GBIT8(p); p += n;
			break;
		case 's': n = sizeof(u16int); if(p + n > e) goto error;
			*va_arg(a, int*) = GBIT16(p); p += n;
			break;
		case 'd': n = sizeof(u32int); if(p + n > e) goto error;
			*va_arg(a, int*) = GBIT32(p); p += n;
			break;
		case 'l': n = sizeof(u32int); if(p + n > e) goto error;
			*va_arg(a, long*) = GBIT32(p); p += n;
			break;
		case 'v': n = sizeof(u64int); if(p + n > e) goto error;
			*va_arg(a, vlong*) = GBIT64(p); p += n;
			break;
		}
		sz += n;
	}
}

static int
unpack(uchar *p, uchar *e, char *fmt, ...)
{
	int n;
	va_list a;

	va_start(a, fmt);
	n = vunpack(p, e, fmt, a);
	va_end(a);
	return n;
}

static Mobj *
mobjfromreq(Mobj *r)
{
	Mobj *mo;

	if((mo = derefmobj(r->idx, r->uuid)) == nil)
		return nil;
	return mo;
}

static int
reqgather(uchar *p, uchar *e)
{
	int n;
	Mobj reqm, reqt, *mo, *tgt;

	if((n = unpack(p, e, MGATHER, &reqm.idx, &reqm.uuid, &reqt.idx, &reqt.uuid)) < 0)
		return -1;
	if((mo = mobjfromreq(&reqm)) == nil)
		return -1;
	if((mo->o->f & Fgather) == 0){
		werrstr("reqgather: object %M not a gatherer", mo);
		return -1;
	}
	if((mo->o->f & Fimmutable) || mo->o->speed == 0.0){
		werrstr("reqgather: object %M can't move", mo);
		return -1;
	}
	if((tgt = mobjfromreq(&reqt)) == nil)
		return -1;
	if(mo == tgt){
		werrstr("reqgather: object %M targeting itself", mo);
		return -1;
	}
	if((tgt->o->f & Fresource) == 0){
		werrstr("reqgather: target %M not a resource", tgt);
		return -1;
	}
	if(pushgathercommand(mo, tgt) < 0)
		return -1;
	return n;
}

static int
reqmovenear(uchar *p, uchar *e)
{
	int n;
	Mobj reqm, reqt, *mo, *tgt;

	if((n = unpack(p, e, MMOVENEAR, &reqm.idx, &reqm.uuid, &reqt.idx, &reqt.uuid)) < 0)
		return -1;
	if((mo = mobjfromreq(&reqm)) == nil)
		return -1;
	if((mo->o->f & Fimmutable) || mo->o->speed == 0.0){
		werrstr("reqmovenear: object %M can't move", mo);
		return -1;
	}
	if((tgt = mobjfromreq(&reqt)) == nil)
		return -1;
	if(mo == tgt){
		werrstr("reqmovenear: object %M targeting itself", mo);
		return -1;
	}
	if(pushmovecommand(mo, tgt->Point, tgt) < 0)
		return -1;
	return n;
}

static int
reqmove(uchar *p, uchar *e)
{
	int n;
	Point tgt;
	Mobj reqm, *mo;

	if((n = unpack(p, e, MMOVE, &reqm.idx, &reqm.uuid, &tgt.x, &tgt.y)) < 0)
		return -1;
	if(!ptinrect(tgt, Rect(0,0,mapwidth,mapheight))){
		werrstr("reqmove: invalid target %P", tgt);
		return -1;
	}
	if((mo = mobjfromreq(&reqm)) == nil)
		return -1;
	if(eqpt(mo->Point, tgt)){
		werrstr("reqmove: object %M targeting itself", mo);
		return -1;
	}
	if((mo->o->f & Fimmutable) || mo->o->speed == 0.0){
		werrstr("reqmove: object %M can't move", mo);
		return -1;
	}
	if(pushmovecommand(mo, tgt, nil) < 0)
		return -1;
	return n;
}

static int
reqstop(uchar *p, uchar *e)
{
	int n;
	Mobj reqm, *mo;

	if((n = unpack(p, e, MSTOP, &reqm.idx, &reqm.uuid)) < 0)
		return -1;
	if((mo = mobjfromreq(&reqm)) == nil)
		return -1;
	clearcommands(mo);
	return n;
}

static int
reqpause(uchar *, uchar *)
{
	pause ^= 1;
	return 0;
}

static int
readhdr(Msg *m, Header *h)
{
	if(unpack(m->buf, m->buf + m->sz, "s", &h->size) < 0
	|| h->size <= 0
	|| h->size != m->sz - Hdrsz){
		werrstr("readhdr: malformed message");
		return -1;
	}
	return 0;
}

int
parsemsg(Msg *m)
{
	int n, type;
	uchar *p, *e;
	int (*fn)(uchar*, uchar*);
	Header h;

	if(readhdr(m, &h) < 0){
		dprint("parsemsg: %r\n");
		return -1;
	}
	p = m->buf + Hdrsz;
	e = m->buf + m->sz;
	while(p < e){
		type = *p++;
		switch(type){
		case CTpause: fn = reqpause; break;
		case CTstop: fn = reqstop; break;
		case CTmove: fn = reqmove; break;
		case CTmovenear: fn = reqmovenear; break;
		case CTgather: fn = reqgather; break;
		case CTeom:
			if(p < e)
				fprint(2, "parsemsg: trailing data\n");
			return 0;
		default: fprint(2, "parsemsg: invalid message type %ux\n", type); return -1;
		}
		if((n = fn(p, e)) < 0){
			fprint(2, "parsemsg: %r\n");
			return -1;
		}else
			p += n;
	}
	return 0;
}

static int
vpack(uchar *p, uchar *e, char *fmt, va_list a)
{
	int n, sz;
	uchar u[8];
	u32int v;
	u64int w;

	sz = 0;
	for(;;){
		n = 0;
		switch(*fmt++){
		default: sysfatal("unknown format %c", fmt[-1]);
		copy: if(p + n > e) sysfatal("vpack: buffer overflow");
			memcpy(p, u, n); p += n; break;
		case 0: return sz;
		case ' ': break;
		case 'h': v = va_arg(a, int); PBIT8(u, v); n = sizeof(u8int); goto copy;
		case 's': v = va_arg(a, int); PBIT16(u, v); n = sizeof(u16int); goto copy;
		case 'd': v = va_arg(a, int); PBIT32(u, v); n = sizeof(u32int); goto copy;
		case 'l': v = va_arg(a, long); PBIT32(u, v); n = sizeof(u32int); goto copy;
		case 'v': w = va_arg(a, vlong); PBIT64(u, w); n = sizeof(u64int); goto copy;
		}
		sz += n;
	}
}

static void
newmsg(Msg *m)
{
	m->sz += Hdrsz;
}

static int
pack(uchar *p, uchar *e, char *fmt, ...)
{
	int n;
	va_list a;

	va_start(a, fmt);
	n = vpack(p, e, fmt, a);
	va_end(a);
	return n;
}

static int
packmsg(Msg *m, char *fmt, ...)
{
	int n;
	va_list a;

	if(m->sz == 0)
		newmsg(m);
	va_start(a, fmt);
	n = vpack(m->buf + m->sz, m->buf + sizeof m->buf, fmt, a);
	va_end(a);
	if(n >= 0)
		m->sz += n;
	return n;
}

void
endmsg(Msg *m)
{
	packmsg(m, "h", CTeom);
	pack(m->buf, m->buf + Hdrsz, "s", m->sz - Hdrsz);
}

int
sendgather(Mobj *mo, Mobj *tgt)
{
	Msg *m;

	m = getclbuf();
	if(packmsg(m, "h" MGATHER, CTgather, mo->idx, mo->uuid, tgt->idx, tgt->uuid) < 0){
		fprint(2, "sendgather: %r\n");
		return -1;
	}
	return 0;
}

int
sendmovenear(Mobj *mo, Mobj *tgt)
{
	Msg *m;

	m = getclbuf();
	if(packmsg(m, "h" MMOVENEAR, CTmovenear, mo->idx, mo->uuid, tgt->idx, tgt->uuid) < 0){
		fprint(2, "sendmovenear: %r\n");
		return -1;
	}
	return 0;
}

int
sendmove(Mobj *mo, Point tgt)
{
	Msg *m;

	m = getclbuf();
	if(packmsg(m, "h" MMOVE, CTmove, mo->idx, mo->uuid, tgt.x, tgt.y) < 0){
		fprint(2, "sendmove: %r\n");
		return -1;
	}
	return 0;
}

int
sendstop(Mobj *mo)
{
	Msg *m;

	m = getclbuf();
	if(packmsg(m, "h" MSTOP, CTstop, mo->idx, mo->uuid) < 0){
		fprint(2, "sendstop: %r\n");
		return -1;
	}
	return 0;
}

int
sendpause(void)
{
	Msg *m;

	m = getclbuf();
	if(packmsg(m, "h", CTpause) < 0){
		fprint(2, "sendpause: %r\n");
		return -1;
	}
	return 0;
}
