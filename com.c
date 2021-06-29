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

static Munit *
getmunit(Munit *r)
{
	int n;
	Munit *mu;
	Team *t;

	n = r->idx >> Teamshift & Nteam - 1;
	if(n > nteam){
		werrstr("invalid team number %d", n);
		return nil;
	}
	t = teams + n;
	n = r->idx & Teamidxmask;
	if(n > t->sz || (mu = t->mu[n]) == nil){
		werrstr("unit index %d out of bounds", n);
		return nil;
	}
	if(mu->idx != r->idx || mu->uuid != r->uuid
	|| mu->x != r->x || mu->y != r->y){
		werrstr("phase error: %s at %d,%d has %#ux,%ld, req has %d,%d %#ux,%ld",
			mu->u->name, mu->x, mu->y, mu->idx, mu->uuid,
			r->x, r->y, r->idx, r->uuid);
		return nil;
	}
	return mu;
}

static int
reqmovenear(uchar *p, uchar *e)
{
	int n;
	Point click;
	Munit reqm, reqt, *mu, *tgt;

	if((n = unpack(p, e, "dldd dd dldd",
	&reqm.idx, &reqm.uuid, &reqm.x, &reqm.y,
	&click.x, &click.y,
	&reqt.idx, &reqt.uuid, &reqt.x, &reqt.y)) < 0)
		goto error;
	if((mu = getmunit(&reqm)) == nil)
		goto error;
	if((tgt = getmunit(&reqt)) == nil)
		goto error;
	if(click.x >= nodemapwidth || click.y >= nodemapheight){
		werrstr("reqmove: invalid location %d,%d", click.x, click.y);
		return -1;
	}
	moveone(click, mu, tgt);
	return n;
error:
	return -1;
}

static int
reqmove(uchar *p, uchar *e)
{
	int n;
	Point tgt;
	Munit reqm, *mu;

	if((n = unpack(p, e, "dldd dd",
	&reqm.idx, &reqm.uuid, &reqm.x, &reqm.y,
	&tgt.x, &tgt.y)) < 0)
		goto error;
	if((mu = getmunit(&reqm)) == nil)
		goto error;
	if(tgt.x >= nodemapwidth || tgt.y >= nodemapheight){
		werrstr("reqmove: invalid target %d,%d", tgt.x, tgt.y);
		return -1;
	}
	moveone(tgt, mu, nil);
	return n;
error:
	return -1;
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
		case Tpause: fn = reqpause; break;
		case Tmove: fn = reqmove; break;
		case Tmovenear: fn = reqmovenear; break;
		case Teom:
			if(p < e)
				dprint("parsemsg: trailing data\n");
			return 0;
		default: dprint("parsemsg: invalid message type %ux\n", type); return -1;
		}
		if((n = fn(p, e)) < 0)
			dprint("parsemsg: %r\n");
		else
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
	packmsg(m, "h", Teom);
	pack(m->buf, m->buf + Hdrsz, "s", m->sz - Hdrsz);
}

int
sendmovenear(Munit *mu, Point click, Munit *tgt)
{
	Msg *m;

	m = getclbuf();
	if(packmsg(m, "h dldd dd dldd", Tmovenear,
	mu->idx, mu->uuid, mu->x, mu->y,
	click.x, click.y,
	tgt->idx, tgt->uuid, tgt->x, tgt->y) < 0){
		fprint(2, "sendmovenear: %r\n");
		return -1;
	}
	return 0;
}

int
sendmove(Munit *mu, Point tgt)
{
	Msg *m;

	m = getclbuf();
	if(packmsg(m, "h dldd dd", Tmove,
	mu->idx, mu->uuid, mu->x, mu->y,
	tgt.x, tgt.y) < 0){
		fprint(2, "sendmove: %r\n");
		return -1;
	}
	return 0;
}

int
sendpause(void)
{
	Msg *m;

	m = getclbuf();
	if(packmsg(m, "h", Tpause) < 0){
		fprint(2, "sendpause: %r\n");
		return -1;
	}
	return 0;
}
