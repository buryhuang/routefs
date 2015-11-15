#ifndef __POST_PROCESS_H__
#define __POST_PROCESS_H__

#include <sys/param.h>
//#include <sys/systm.h>

#include <cstdarg>
#include "store.h"
#include <vector>

using namespace std;

struct PP_ENTRY_T
{
	std::string store_path[MAX_STORE_LEVEL];
	int state;
	uint_least64_t obj_id;
};

#define POSTPROCESS_DB (STORE_ROOT + "/.postprocess")

extern int postprocess_init();
extern int postprocess_set(const char * obj, const int state, std::string store_path1);
extern int postprocess_set(const char * obj, const int state, std::string store_path1, std::string store_path2);
extern int postprocess_set(const char * obj, const int state, std::string store_path1, std::string store_path2, std::string store_path3);
extern int postprocess_set(const char * obj, const int state, std::string store_path1, std::string store_path2, std::string store_path3, std::string store_path4);
extern int postprocess_set(const char * obj, const int state, std::string store_path1, std::string store_path2, std::string store_path3, std::string store_path4, std::string store_path5);
extern int postprocess_get(const char * obj, const PP_ENTRY_T &pp_entry);
/*
 * Not a typo.
 * Having pp_entry here is to avoid deleting updated objects, by checking gid in the pp_entry
 */
extern int postprocess_del(const char * obj, const PP_ENTRY_T *pp_entry);
extern int postprocess_list(const char * prefix, vector<string>& obj_list);
extern int postprocess_dump_to_log();

// @todo: security...?
extern void * postprocess_hdl();

#endif