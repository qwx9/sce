</$objtype/mkfile
BIN=$home/bin/$objtype
TARG=sce
OFILES=\
	bmap.$O\
	com.$O\
	drw.$O\
	fs.$O\
	map.$O\
	net.$O\
	path.$O\
	pheap.$O\
	sce.$O\
	sim.$O\
	sim.idle.$O\
	sim.move.$O\
	sim.resource.$O\
	sim.spawn.$O\
	snd.$O\
	sv.$O\
	util.$O\

HFILES=dat.h fns.h
</sys/src/cmd/mkone

#LDFLAGS=$LDFLAGS -p

sysinstall:V:
	mkdir -p /sys/games/lib/sce
	dircp sce /sys/games/lib/sce
