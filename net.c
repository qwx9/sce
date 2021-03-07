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
	Ncon = Nteam * 2,
};
struct Con{
	int fd;
	Msg;
	Channel *waitc;
};
static Con con[Ncon];
static Msg clbuf, sendbuf;
Channel *conc;

static int clpfd[2];

static void
closenet(Con *c)
{
	close(c->fd);
	c->fd = -1;
	chanfree(c->waitc);
	c->waitc = nil;
}

static void
flushreq(Con *c, Cbuf *cb)
{
	if(cb->sz == 0)
		return;
	if(write(c->fd, cb->buf, cb->sz) != cb->sz){
		fprint(2, "flushcmd: %r\n");
		close(c->fd);
	}
}

static void
writenet(void)
{
	Con *c;

	for(c=con; c<con+nelem(con); c++){
		if(c->fd < 0)
			continue;
		flushreq(c, &sendbuf);
	}
	sendbuf.sz = 0;
}

void
flushcl(void)
{
	if(clbuf.sz == 0)
		return;
	write(clpfd[0], clbuf.buf, clbuf.sz);
	clbuf.sz = 0;
}

void
clearmsg(Msg *m)
{
	Con *c;

	for(c=con; c<con+sizeof con; c++)
		if(m == &c->Msg)
			break;
	assert(c < con + sizeof con);
	m->sz = 0;
	nbsendul(c->waitc, 0);
}

Msg *
readnet(void)
{
	Con *c;

	if((c = nbrecvp(conc)) == nil)
		return nil;
	return &c->Msg;
}

Msg *
getclbuf(void)
{
	return &clbuf;
}

static void
conproc(void *cp)
{
	int n;
	Con *c;

	c = cp;
	for(;;){
		if((n = read(c->fd, c->buf, sizeof c->buf)) <= 0){
			fprint(2, "cproc %zd: %r\n", c - con);
			closenet(c);
			return;
		}
		c->sz = n;
		sendp(conc, c);
		recvul(c->waitc);
		c->sz = 0;
	}
}

static void
initcon(Con *c)
{
	if((c->waitc = chancreate(sizeof(ulong), 0)) == nil)
		sysfatal("chancreate: %r");
	if(proccreate(conproc, c, 8192) < 0)
		sysfatal("proccreate: %r");
}

static int
regnet(int lfd, char *ldir)
{
	Con *c;

	for(c=con; c<con+nelem(con); c++)
		if(c->fd < 0)
			break;
	if(c == con + nelem(con)){
		reject(lfd, ldir, "no more open slots");
		return -1;
	}
	c->fd = accept(lfd, ldir);
	initcon(c);
	return 0;
}

static void
listenproc(void *)
{
	int lfd;
	char adir[40], ldir[40], data[100];

	snprint(data, sizeof data, "%s/tcp!*!%d", netmtpt, lport);
	if(announce(data, adir) < 0)
		sysfatal("announce: %r");
	for(;;){
		if((lfd = listen(adir, ldir)) < 0
			|| regnet(lfd, ldir) < 0
			|| close(lfd) < 0)
			break;
	}
}

static void
listennet(void)
{
	if(proccreate(listenproc, nil, 8192) < 0)
		sysfatal("proccreate: %r");
}

static void
joinnet(char *sys)
{
	char s[128];
	Con *c;

	snprint(s, sizeof s, "%s/tcp!%s!%d", netmtpt, sys != nil ? sys : sysname(), lport);
	c = &con[1];
	if((c->fd = dial(s, nil, nil, nil)) < 0)
		sysfatal("dial: %r");
	initcon(c);
}

void
initnet(char *sys)
{
	Con *c;

	for(c=con; c<con+nelem(con); c++)
		c->fd = -1;
	if((conc = chancreate(sizeof(uintptr), 1)) == nil)
		sysfatal("chancreate: %r");
	if(pipe(clpfd) < 0)
		sysfatal("pipe: %r");
	con[0].fd = clpfd[1];
	initcon(&con[0]);
	USED(sys);
	//joinnet(sys);
	//listennet();
}
