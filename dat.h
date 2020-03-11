typedef struct Attack Attack;
typedef struct Pic Pic;
typedef struct Pics Pics;
typedef struct Obj Obj;
typedef struct Mobj Mobj;
typedef struct Lobj Lobj;
typedef struct Terrain Terrain;
typedef struct Map Map;
typedef struct Resource Resource;
typedef struct Team Team;
typedef struct Path Path;

enum{
	Nresource = 3,
	Nteam = 8,
	Nselect = 12,
	Nrot = 32,
	Npath = 64,
	Tlwidth = 32,
	Tlheight = Tlwidth,
	Tlshift = 16,
	Tlmask = ((1 << Tlshift) - 1) << Tlshift,
	Tlsubshift = 2,
	Tlsubwidth = Tlwidth >> Tlsubshift,
	Tlsubheight = Tlheight >> Tlsubshift,
	Tlsubmask = Tlsubwidth - 1,
};

struct Attack{
	char *name;
	int dmg;
	int range;
	int cool;
};

struct Pic{
	u32int *p;
	int w;
	int h;
	int dx;
	int dy;
};
struct Pics{
	Pic *p;
	int nf;
	int nr;
	int n;
};

enum{
	Fbio = 1<<0,
	Fmech = 1<<1,
	Fair = 1<<2,
	Fbuild = 1<<3,
};
struct Obj{
	char *name;
	Pics pidle;
	Pics pmove;
	Pics patk;
	Pics pgather;
	Pics pdie;
	Pics pburrow;
	int nf;
	int nr;
	Attack *atk[2];
	int f;
	int w;
	int h;
	int hp;
	int def;
	int speed;
	int vis;
	int cost[Nresource];
	int time;
	Obj **spawn;
	int nspawn;
};

struct Mobj{
	Obj *o;
	Pics *pics;
	Point;
	Point p;
	Point subp;
	int θ;
	double vx;
	double vy;
	double vv;
	int Δθ;
	Point *path;
	Point *pathp;
	Point *pathe;
	int f;
	int team;
	int hp;
	int xp;
	Lobj *blk;
	Lobj *zl;
	Lobj *vl;
};

struct Lobj{
	Mobj *mo;
	Lobj *lo;
	Lobj *lp;
};
extern Lobj zlist;

struct Terrain{
	char *name;
	Pic pic;
	int f;
	Resource *r;
};

struct Map{
	Point;
	int tx;
	int ty;
	Terrain *t;
	Lobj lo;
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

struct Path{
	Point;
	Lobj lo;
	Mobj *blk;
	int closed;
	int open;
	Path *from;
	double g;
	double h;
};
extern Path *path;
extern int pathwidth, pathheight;

extern int lport;
extern char *netmtpt;

extern int scale;

enum{
	Tquit,
};

extern char *progname, *prefix, *dbname, *mapname;
extern int clon;
extern vlong tc;
