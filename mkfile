</$objtype/mkfile
BIN=$home/bin/$objtype
TARG=sce
OFILES=\
	ai.$O\
	drw.$O\
	fs.$O\
	net.$O\
	sce.$O\
	sim.$O\
	snd.$O\

HFILES=dat.h fns.h
</sys/src/cmd/mkone

#LDFLAGS=$LDFLAGS -p

sysinstall:V:
	mkdir -p /sys/games/lib/sce
	dircp sce /sys/games/lib/sce
