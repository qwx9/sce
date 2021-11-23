typedef struct Node Node;
typedef struct Pairheap Pairheap;
typedef struct Attack Attack;
typedef struct Pic Pic;
typedef struct Pics Pics;
typedef struct Obj Obj;
typedef struct Path Path;
typedef struct Command Command;
typedef struct Munit Munit;
typedef struct Mresource Mresource;
typedef struct Mobj Mobj;
typedef struct Mobjl Mobjl;
typedef struct Tile Tile;
typedef struct Map Map;
typedef struct Resource Resource;
typedef struct Team Team;
typedef struct Cbuf Cbuf;
typedef struct Msg Msg;

enum{
	Nresource = 3,
	Nselect = 12,
	Nrot = 32,
	/* oh boy */
	Nplayteam = 8,		/* non-neutral player teams */
	Nteambits = 4,
	Nteam = 1 << Nteambits,
	Teamshift = 32 - Nteambits,
	Teamidxmask = ~(Nteam - 1 << Teamshift),
	Tilewidth = 32,
	Tileheight = Tilewidth,
	Node2Tile = 4,
	Nodewidth = Tilewidth / Node2Tile,
	Nodeheight = Tileheight / Node2Tile,
	Subpxshift = 16,
	Subpxmask = (1 << Subpxshift) - 1,
};

enum{
	Bshift = 6,
	Bmask = (1 << Bshift) - 1,
};

struct Pairheap{
	double sum;
	Node *n;
	Pairheap *parent;
	Pairheap *left;
	Pairheap *right;
};

struct Node{
	int x;
	int y;
	int closed;
	int open;
	double g;
	double Δg;
	double h;
	double len;
	double Δlen;
	int step;
	int dir;
	Node *from;
	Pairheap *p;
};
extern Node *nodemap;
extern int nodemapwidth, nodemapheight;

struct Pic{
	u32int *p;
	int w;
	int h;
	int dx;
	int dy;
};
struct Pics{
	Pic **pic;
	int teamcol;
	int nf;
	int nr;
	int shared;
	int freeze;
};

struct Attack{
	char *name;
	int dmg;
	int range;
	int cool;
};

enum{
	Fbio = 1<<0,
	Fmech = 1<<1,
	Fair = 1<<2,
	Fbuild = 1<<3,
	Fgather = 1<<4,
	Fresource = 1<<14,
	Fimmutable = 1<<15,
};
enum{
	PTbase,
	PTshadow,
	PTglow,
	PTend,

	OState0 = 0,
	OState1,
	OState2,
	OState3,
 	OSend,
 	OSskymaybe = 666,

	/* unit */
	OSidle = OState0,
	OSmove = OState1,
	OSgather = OState2,
	OSwait = OState3,	/* FIXME: ← better solution */

	/* resource */
	OSrich = OState0,
	OSmed = OState1,
	OSlow = OState2,
	OSpoor = OState3,
};
enum{
	PFidle = OSidle,
	PFmove = OSmove,
	PFgather = OSgather,
	PFrich = OSrich,
	PFmed = OSmed,
	PFlow = OSlow,
	PFpoor = OSpoor,
	PFstatemask = (1 << 5) - 1,

	PFfreezepic = 1<<11,
	PFimmutable = 1<<12,
	PFglow = 1<<13,
	PFalpha = 1<<14,
	PFshadow = 1<<15,
	PFtile = 1<<16,

	Ncmd = 32,
};
struct Obj{
	char *name;
	Pics pics[OSend][PTend];
	int w;
	int h;
	int f;
	Attack *atk[2];
	int hp;
	int def;
	int vis;
	int cost[Nresource];
	int time;
	double speed;
	double accel;
	double halt;
	double turn;
	Obj **spawn;
	int nspawn;
	Resource *res;
};
struct Path{
	Point target;
	int goalblocked;
	int npatherr;
	int npathbuf;
	double pathlen;
	Point *paths;
	Point *pathp;
	Point *pathe;
};
struct Mresource{
	int amount;
};
struct Munit{
	int team;
	int hp;
	int xp;
	int freezefrm;
	double θ;
	double Δθ;
	int Δθs;
	Path;
	double u;
	double v;
	double speed;
	Mobjl *mobjl;
	Mobjl *mapl;
};
struct Command{
	char *name;
	Point goal;
	Mobj *target1;
	Mobj *target2;
	vlong tc;
	int (*initfn)(Mobj*);
	void (*stepfn)(Mobj*);
	void (*cleanupfn)(Mobj*);
	int (*nextfn)(Mobj*);
};
struct Mobj{
	Obj *o;
	int idx;
	long uuid;
	int state;
	Command cmds[Ncmd];
	int ctail;
	Point;
	int px;
	int py;
	int subpx;
	int subpy;
	Munit;
	Mresource;
};
struct Mobjl{
	Mobj *mo;
	Mobjl *l;
	Mobjl *lp;
};
extern char *statename[OSend];

struct Tile{
	Pic *p;
};
struct Map{
	Tile *t;
	Mobjl ml;
};
extern Map *map;
extern int mapwidth, mapheight;

struct Resource{
	char *name;
	int init;
	Obj **obj;
	int nobj;
	int *thresh;
	int nthresh;
};
extern Resource resources[Nresource];

struct Team{
	int r[Nresource];
	int nunit;
	int nbuild;
	Mobj **mo;
	int sz;
	int firstempty;
};
extern Team teams[Nteam];
extern int nteam;

extern int lport;
extern char *netmtpt;

extern int scale;

enum{
	CTquit = 0x1f,
	CTpause,
	CTstop,
	CTmove,
	CTmovenear,
	CTgather,
	CTeom,

	Nbuf = 4096,
};
struct Cbuf{
	uchar buf[Nbuf];
	int sz;
};
struct Msg{
	Team *t;
	Cbuf;
};

enum{
	Te9 = 1000000000,
	Te6 = 1000000,
	Te3 = 1000,

	Tfast = 6,
};
extern char *progname, *prefix, *dbname, *mapname;
extern vlong tc;
extern int pause, debugmap;
extern int debug;
