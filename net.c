#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

int lport = 17996;
char *netmtpt = "/net";

typedef struct Con Con;
enum{
	Ncon = Nteam * 4,
	Nbuf = 4096,
};
struct Con{
	int fd;
	Team *t;
};
static Con con[Ncon];
static Channel *lc;
static uchar cbuf[Nbuf], *cbufp = cbuf;

static void
closenet(Con *c)
{
	close(c->fd);
	c->fd = -1;
}

static void
flushcmd(Con *c)
{
	int n;

	if((n = cbufp - cbuf) == 0)
		return;
	if(write(c->fd, cbuf, n) != n){
		fprint(2, "flushcmd: %r\n");
		closenet(c);
	}
	cbufp = cbuf;
}

static void
writecmd(Con *c, char *fmt, va_list a)
{
	union{
		uchar u[4];
		s32int l;
	} u;

	for(;;){
		if(cbufp - cbuf > sizeof(cbuf) - 4)
			flushcmd(c);
		switch(*fmt++){
		default: sysfatal("unknown format %c", fmt[-1]);
		case 0: return;
		case 'u':
			u.l = va_arg(a, s32int);
			memcpy(cbufp, u.u, sizeof u.u);
			cbufp += sizeof u.u;
		}
	}
}

static void
packcmd(Con *c, char *fmt, ...)
{
	va_list a;

	va_start(a, fmt);
	writecmd(c, fmt, a);
	va_end(a);
}

void
flushcl(void)
{
	flushcmd(con);
}

void
packcl(char *fmt, ...)
{
	va_list a;

	va_start(a, fmt);
	packcmd(con, fmt, a);
	va_end(a);
}

static void
writenet(void)
{
	Con *c;

	for(c=con; c<con+nelem(con); c++){
		if(c->fd < 0)
			continue;
	}
}

static void
execbuf(Con *c, uchar *p, uchar *e)
{
	int n;

	while(p < e){
		n = *p++;
		switch(n){
		case Tquit: closenet(c); return;
		}
	}
}

static void
readnet(void)
{
	int n, m;
	Con *c;
	uchar buf[Nbuf], *p;

	for(c=con; c<con+nelem(con); c++){
		if(c->fd < 0)
			continue;
		for(m=sizeof buf, p=buf; (n = flen(c->fd)) > 1 && n <= m; m-=n, p+=n){
			if(read(c->fd, p, n) != n){
				fprint(2, "readnet: %r\n");
				closenet(c);
				break;
			}
		}
		execbuf(c, buf, p);
	}
}

void
stepnet(void)
{
	int fd;

	if(nbrecv(lc, &fd) > 0){
		/* FIXME: add to observers (team 0) */
	}
	readnet();
	writenet();
}

void
joinnet(char *sys)
{
	char s[128];

	snprint(s, sizeof s, "%s/tcp!%s!%d", netmtpt, sys != nil ? sys : sysname(), lport);
	if((con[0].fd = dial(s, nil, nil, nil)) < 0)
		sysfatal("dial: %r");
}

static int
regnet(int lfd, char *ldir)
{
	Con *c;

	for(c=con; c<con+nelem(con); c++)
		if(c->fd < 0)
			break;
	if(c == con + nelem(con))
		return reject(lfd, ldir, "no more open slots");
	c->fd = accept(lfd, ldir);
	return c->fd;
}

static void
lproc(void *)
{
	int fd, lfd;
	char adir[40], ldir[40], data[100];

	snprint(data, sizeof data, "%s/tcp!*!%d", netmtpt, lport);
	if(announce(data, adir) < 0)
		sysfatal("announce: %r");
	for(;;){
		if((lfd = listen(adir, ldir)) < 0
			|| (fd = regnet(lfd, ldir)) < 0
			|| close(lfd) < 0
			|| send(lc, &fd) < 0)
			break;
	}
}

void
listennet(void)
{
	Con *c;

	for(c=con; c<con+nelem(con); c++)
		c->fd = -1;
	if((lc = chancreate(sizeof(int), 0)) == nil)
		sysfatal("chancreate: %r");
	if(proccreate(lproc, nil, 8192) < 0)
		sysfatal("proccreate: %r");
}
