#include <u.h>
#include <libc.h>
#include <draw.h>
#include <fcall.h>
#include "dat.h"
#include "fns.h"

enum{
	Hdrsz = 4,
};
typedef struct Header Header;
struct Header{
	int empty;
};

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
		case 'h': v = va_arg(a, int); PBIT8(u, v); n = sizeof(u8int); goto copy;
		case 's': v = va_arg(a, int); PBIT16(u, v); n = sizeof(u16int); goto copy;
		case 'l': v = va_arg(a, int); PBIT32(u, v); n = sizeof(u32int); goto copy;
		case 'v': w = va_arg(a, vlong); PBIT64(u, w); n = sizeof(u64int); goto copy;
		}
		sz += n;
	}
}

static int
vunpack(uchar *p, uchar *e, char *fmt, va_list a)
{
	int n, sz;

	sz = 0;
	for(;;){
		switch(*fmt++){
		default: sysfatal("vunpack: unknown format %c", fmt[-1]);
		error: werrstr("vunpack: truncated message"); return -1;
		case 0: return sz;
		case 'h': n = sizeof(u8int); if(p + n > e) goto error;
			*va_arg(a, int*) = GBIT8(p); p += n;
			break;
		case 's': n = sizeof(u16int); if(p + n > e) goto error;
			*va_arg(a, int*) = GBIT16(p); p += n;
			break;
		case 'l': n = sizeof(u32int); if(p + n > e) goto error;
			*va_arg(a, int*) = GBIT32(p); p += n;
			break;
		case 'v': n = sizeof(u64int); if(p + n > e) goto error;
			*va_arg(a, vlong*) = GBIT64(p); p += n;
			break;
		}
		sz += n;
	}
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
unpack(uchar *p, uchar *e, char *fmt, ...)
{
	int n;
	va_list a;

	va_start(a, fmt);
	n = vunpack(p, e, fmt, a);
	va_end(a);
	return n;
}

static int
reqpause(uchar *p, uchar *e)
{
	int dicks;

	if(unpack(p, e, "l", &dicks) < 0){
		fprint(2, "reqpause: %r\n");
		return -1;
	}
	pause ^= 1;
	return 0;
}

static int
readhdr(Msg *m, Header *h)
{
	USED(h);
	if(m->sz <= Hdrsz)
		return -1;
	return 0;
}

int
parsemsg(Msg *m)
{
	int n, type;
	uchar *p, *e;
	Header h;

	if(readhdr(m, &h) < 0)
		return -1;
	p = m->buf + Hdrsz;
	e = p + sizeof(m->buf) - Hdrsz;
	while(p < e){
		type = *p++;
		switch(type){
		case Tpause:
			if((n = reqpause(p, e)) < 0){
				dprint("parse: invalid Tpause: %r\n");
				return -1;
			}
			break;
		default:
			dprint("parse: invalid message type %ux\n", type);
			return -1;
		}
		p += n;
	}
	return 0;
}

static void
newmsg(Msg *m)
{
	Header h;

	USED(h);
	m->sz += Hdrsz;
}

int
sendpause(void)
{
	int n;
	Msg *m;

	m = getclbuf();
	if(m->sz == 0)
		newmsg(m);
	if((n = pack(m->buf + m->sz, m->buf + sizeof m->buf, "hl", Tpause, 0)) < 0){
		fprint(2, "sendpause: %r\n");
		return -1;
	}
	m->sz += n;
	return 0;
}
