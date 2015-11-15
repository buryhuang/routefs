#ifndef __OBJ_MAP_H__
#define __OBJ_MAP_H__

#include <sys/param.h>
//#include <sys/systm.h>

#include "store.h"
#include <vector>

using namespace std;

#define OBJMAP_DB (STORE_ROOT + "/.objmap")

//#ifdef CACHE_MODE
#define OBJMAP_DB2 (STORE_ROOT + "/.objmap2")
//#endif

extern int objmap_init();
extern int objmap_set(const char * obj, const char * dest, int level = 1);
extern int objmap_get(const char * obj, string &destStr, int level = 1);
extern int objmap_del(const char * obj, int level = 1);
extern int objmap_list(const char * prefix, vector<string>& obj_list, int level);
extern int objmap_dump_to_log(int level);

extern void * objmap_hdl(int level);

#endif
