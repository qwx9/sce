void	stepsnd(void);
void	initsnd(void);
void	llink(Lobj*, Lobj*);
void	lunlink(Lobj*);
void	stepsim(void);
void	initsv(void);
void	flushcl(void);
void	packcl(char*, ...);
void	stepnet(void);
void	joinnet(char*);
void	listennet(void);
void	dopan(int, int);
void	redraw(void);
void	resetfb(void);
void	drawfb(void);
void	initimg(void);
void	init(void);
int	isblocked(Point*, Mobj*);
void	setblk(Mobj*, int);
int	findpath(Mobj*, Point*);
void	initpath(void);
int	move(int, int, Mobj**);
int	spawn(Map*, Obj*, int);
void	inittab(void);
char*	estrdup(char*);
void*	emalloc(ulong);
vlong	flen(int);