typedef struct Node Node;
typedef struct Pairheap Pairheap;
typedef struct Attack Attack;
typedef struct Pic Pic;
typedef struct Pics Pics;
typedef struct Obj Obj;
typedef struct Path Path;
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
	Nteam = 8,
	Nselect = 12,
	Nrot = 32,
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
extern Node *node;

struct Attack{
	char *name;
	int dmg;
	int range;
	int cool;
};

enum{
	PFtile = 1<<0,
	PFidle = 1<<1,
	PFmove = 1<<2,
	PFglow = 1<<13,
	PFalpha = 1<<14,
	PFshadow = 1<<15,
};
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
	int iscopy;
};

enum{
	Fbio = 1<<0,
	Fmech = 1<<1,
	Fair = 1<<2,
	Fbuild = 1<<3,
};
enum{
	PTbase,
	PTshadow,
	PTglow,
	PTend,

	OSidle = 0,
	OSmove,
	OSend,
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
struct Mobj{
	Obj *o;
	int state;
	int freezefrm;
	Point;
	int px;
	int py;
	int subpx;
	int subpy;
	double θ;
	double Δθ;
	int Δθs;
	Path;
	double u;
	double v;
	double speed;
	Mobjl *movingp;
	Mobjl *mapp;
	int f;
	int team;
	int hp;
	int xp;
};
struct Mobjl{
	Mobj *mo;
	Mobjl *l;
	Mobjl *lp;
};

struct Tile{
	Pic *p;
};
extern Tile **tilemap;
extern tilemapwidth, tilemapheight;

struct Map{
	Mobjl ml;
};
extern Map *map;
extern int mapwidth, mapheight;

struct Resource{
	char *name;
	int init;
};
extern Resource resource[Nresource];

struct Team{
	int r[Nresource];
	int nunit;
	int nbuild;
};
extern Team team[Nteam], *curteam;
extern int nteam;

extern int lport;
extern char *netmtpt;

extern int scale;

enum{
	Tquit,
	Tpause,

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
