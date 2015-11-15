#ifndef __STATS_H__
#define __STATS_H__

#include <sys/param.h>
//#include <sys/systm.h>

#include "store.h"
#include <vector>

using namespace std;

#define STATS_DB (STORE_ROOT + "/.stats")

extern int stats_init();
extern int stats_set(const char * obj, const char * state);
extern int stats_get(const char * obj, string &state);
extern int stats_del(const char * obj);
extern int stats_list(const char * prefix, vector<string>& obj_list);
extern int stats_dump_to_log();

// @todo: security...?
extern void * stats_hdl();

#endif