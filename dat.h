typedef struct Node Node;
typedef struct Pairheap Pairheap;
typedef struct Attack Attack;
typedef struct Pic Pic;
typedef struct Pics Pics;
typedef struct Obj Obj;
typedef struct Path Path;
typedef struct Mobj Mobj;
typedef struct Mobjl Mobjl;
typedef struct Terrain Terrain;
typedef struct Map Map;
typedef struct Resource Resource;
typedef struct Team Team;

enum{
	Nresource = 3,
	Nteam = 8,
	Nselect = 12,
	Nrot = 32,
	Tlwidth = 32,
	Tlheight = Tlwidth,
	Tlsubshift = 2,
	Tlsubwidth = Tlwidth >> Tlsubshift,
	Tlsubheight = Tlheight >> Tlsubshift,
	Tlnsub = Tlwidth / Tlsubwidth,
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
	PFterrain = 1<<0,
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

struct Terrain{
	Pic *p;
};
extern Terrain **terrain;
extern terwidth, terheight;

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
};

extern char *progname, *prefix, *dbname, *mapname;
extern int clon;
extern vlong tc;
extern int pause, debugmap;
extern int debug;
