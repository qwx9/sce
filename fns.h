void	clearmsg(Msg*);
Msg*	readnet(void);
void	initnet(char*);
int	parsemsg(Msg*);
void	endmsg(Msg*);
int	sendmovenear(Mobj*, Point, Mobj*);
int	sendmove(Mobj*, Point);
int	sendpause(void);
void	stepsnd(void);
void	initsnd(void);
void	linktomap(Mobj*);
int	moveone(Point, Mobj*, Mobj*);
void	stepsim(void);
void	initsim(void);
void	initsv(int, char*);
void	flushcl(void);
void	packcl(char*, ...);
Msg*	getclbuf(void);
void	dopan(Point);
void	select(Point);
void	move(Point);
void	compose(int, int, u32int);
void	redraw(void);
void	updatefb(void);
void	resetfb(void);
void	drawfb(void);
void	initimg(void);
void	initfs(void);
void	setgoal(Point*, Mobj*, Mobj*);
int	isblocked(int, int, Obj*);
void	markmobj(Mobj*, int);
int	findpath(Point, Mobj*);
Mobj*	mapspawn(int, int, Obj*);
void	initmap(void);
int	spawn(int, int, Obj*, int);
void	nukequeue(Pairheap**);
Pairheap*	popqueue(Pairheap**);
void	decreasekey(Pairheap*, double, Pairheap**);
void	pushqueue(Node*, Pairheap**);
int	lsb(uvlong);
int	msb(uvlong);
u64int*	baddr(int, int);
u64int*	rbaddr(int, int);
u64int*	bload(int, int, int, int, int, int, int, int);
void	bset(int, int, int, int, int);
void	initbmap(void);
void	dprint(char *, ...);
int	max(int, int);
int	min(int, int);
char*	estrdup(char*);
void*	erealloc(void*, ulong, ulong);
void*	emalloc(ulong);
vlong	flen(int);

#pragma	varargck	argpos	dprint	1
