#ifndef __STORE_H__
#define __STORE_H__

#include <sys/types.h>
#include <fuse.h>
#include <map>
#include <string>
#include <vector>

using namespace std;

// Data Store information

struct STORE_T {
	const string store_name;
	bool is_cached;
};

extern string STORE_ROOT;

extern string STORE_DATA_STAGING_ROOT;
extern STORE_T STORE_DATA_STAGING_SOURCE;
extern STORE_T STORE_DATA_STAGING_TARGET;

const unsigned int MAX_STORE_LEVEL = 5;

extern vector<string> STORE_STORE_PATH;
extern vector<string> STORE_VOL_PATH;

extern int store_init(const char * root);
extern int store_mkdir(const char *path, mode_t mode);
extern int store_rename(const char *path, const char *newpath);
extern int store_readdir(
		const char *path,
		void *buf,
		fuse_fill_dir_t filler,
		off_t offset,
		struct fuse_file_info *fi,
		map<string, int>& parent_files);
extern int store_rmdir(const char *path);
extern int store_migrate(const char *path, const char * from_store, const char * to_store, int keep_source);

extern int store_is_valid_store(const char * store_path);

#endif