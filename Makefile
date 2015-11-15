OUTPUT_DIR = bin/

FUSE_PKG_CFLAGS = `PKG_CONFIG_PATH=/usr/lib/x86_64-linux-gnu/pkgconfig/ pkg-config fuse --cflags`
FUSE_PKG_LIBS = `PKG_CONFIG_PATH=/usr/lib/x86_64-linux-gnu/pkgconfig/ pkg-config fuse --libs`

CFLAGS = -O0 -g ${FUSE_PKG_CFLAGS} -DCACHE_MODE -std=c++11
LIBS = -lpthread -ldl -lrt leveldb/libleveldb.a ${FUSE_PKG_LIBS}
OBJS = log.o store.o rootmap.o objmap.o postprocess.o ppd.o stats.o
EXECUTABLES = routefs ppd ifsctl

all : ${EXECUTABLES}

routefs : routefs.c log.h params.h ${OBJS}
	g++ ${CFLAGS} routefs.c -o routefs ${OBJS} ${LIBS}

log.o : log.c log.h params.h
	g++ ${CFLAGS}  -Wall ${FUSE_PKG_CFLAGS} -c log.c

store.o : store.c store.h
	g++ ${CFLAGS}  -Wall ${FUSE_PKG_CFLAGS} -c store.c

rootmap.o : rootmap.c rootmap.h store.h
	g++ ${CFLAGS}  -Wall ${FUSE_PKG_CFLAGS} -c rootmap.c

objmap.o : objmap.c objmap.h store.h
	cd leveldb;make
	g++ ${CFLAGS} -Wall ${FUSE_PKG_CFLAGS} -c objmap.c -I leveldb/include -lpthread 

postprocess.o : postprocess.c postprocess.h store.h
	cd leveldb;make
	g++ ${CFLAGS} -Wall ${FUSE_PKG_CFLAGS} -c postprocess.c -I leveldb/include -lpthread 

stats.o : stats.c stats.h
	cd leveldb;make
	g++ ${CFLAGS} -Wall ${FUSE_PKG_CFLAGS} -c stats.c -I leveldb/include -lpthread 

ppd.o: ppd.cpp
	cd leveldb;make
	g++ ${CFLAGS} -Wall ${FUSE_PKG_CFLAGS} -c ppd.cpp -I leveldb/include -lpthread 

ppd: ppd_main.c ${OBJS}
	cd leveldb;make
	g++ ${CFLAGS} -Wall ${FUSE_PKG_CFLAGS} ppd_main.c ${OBJS} ${LIBS} -o ppd -I leveldb/include -lpthread 

ifsctl: ifsctl.c ${OBJS}
	g++ ${CFLAGS} -Wall ${FUSE_PKG_CFLAGS} ifsctl.c ${OBJS} ${LIBS} -o ifsctl -lpthread 

clean:
	rm -f *.o ${EXECUTABLES}

test: rootmap.c rootmap.h ${OBJS}
	cd leveldb;make
	g++ ${CFLAGS} -Wall ${FUSE_PKG_CFLAGS} ldb_test.cpp ${LIBS} -o ldb_test -I leveldb/include -lpthread
	g++ ${CFLAGS} -Wall ${FUSE_PKG_CFLAGS} cachelayer_test.c ${OBJS} ${LIBS} -o cachelayer_test -lpthread 
	gcc -Wall fusexmp.c ${FUSE_PKG_CFLAGS} ${FUSE_PKG_LIBS} -o fusexmp


dist:
