#ifndef __TYPE_MAP_H__
#define __TYPE_MAP_H__

#include <sys/param.h>
//#include <sys/systm.h>

#include "store.h"
#include <map>
#include <string>

using namespace std;

extern string TYPEMAP_DB;

typedef uint32_t TYPE_T;
typedef map<string, uint32_t> TYPEVAL_T;
typedef map<TYPE_T, string>   TYPEMAP_T;

extern const char * rootmap_getdest(const char * hint);
extern int rootmap_init (const char * default_root);
extern const string rootmap_gettype_str();
extern const string rootmap_getmap_str();

#define TYPE_DEFAULT "*"

#endif