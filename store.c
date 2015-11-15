#include "store.h"
#include "log.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <libgen.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/xattr.h>
#include <time.h>

#include "objmap.h"
#include <string>
#include <vector>

using namespace std;

// Global variables
string STORE_ROOT = "/";
string STORE_DATA_STAGING_ROOT;

// For writing
#define DIRECTIO_BLOCK_SIZE 256*1024

#ifdef CACHE_MODE
STORE_T STORE_DATA_STAGING_SOURCE = { STORE_DATA_STAGING_ROOT, true};
#else
STORE_T STORE_DATA_STAGING_SOURCE = { STORE_DATA_STAGING_ROOT, false};
#endif

STORE_T STORE_DATA_STAGING_TARGET = { STORE_ROOT, false};

vector<string> STORE_STORE_PATH;

vector<string> STORE_VOL_PATH;

// Report errors to logfile and give -errno to caller
static int store_error(const char *str)
{
	int ret = -errno;
	log_msg(LOG_LEVEL_ERROR, "    ERROR %s: %s\n", str, strerror(errno));
	return ret;
}

int store_init(const char * root)
{
	STORE_ROOT = root;
	STORE_DATA_STAGING_ROOT = STORE_ROOT + "/staging";
	return 0;
}

int store_is_valid_store(const char * store_path)
{
	vector<string>::iterator vit;
	for (vit = STORE_VOL_PATH.begin(); vit != STORE_VOL_PATH.end(); vit ++) {
		if(*vit == store_path) {
			return 1;
		}
	}
	// no match found
	return 0;
}

// Create directories in all data stores
int store_mkdir(const char *path, mode_t mode)
{
	int retstat = 0;
	char fpath[PATH_MAX];

	log_msg(LOG_LEVEL_DEBUG, "\nstore_mkdir(path=\"%s\", mode=0%3o)\n",
		path, mode);

	vector<string>::iterator vit;
	for (vit = STORE_VOL_PATH.begin(); vit != STORE_VOL_PATH.end(); vit ++) {
		string curr_dir = *vit;
		struct stat statbuf;
		retstat = lstat(curr_dir.c_str(), &statbuf);
		if (retstat != 0) {
			// Skip those not yet created stores
			retstat = 0;
			continue;
		}
		strcpy(fpath, curr_dir.c_str());
		strncat(fpath, path, PATH_MAX - 1); // ridiculously long paths will break here
		retstat = mkdir(fpath, mode);
		if (retstat < 0) {
			retstat = store_error("store_mkdir mkdir");
			return retstat;
		}
	}
	return retstat;
}

// Create directories in all data stores
int store_rename(const char *path, const char *newpath)
{
	int retstat = 0;
	char fpath[PATH_MAX];
	char fnewpath[PATH_MAX];

	log_msg(LOG_LEVEL_DEBUG, "\nstore_rename(path=\"%s\", newpath=\"%s\")\n", path, newpath);

	vector<string>::iterator vit;
	for (vit = STORE_VOL_PATH.begin(); vit != STORE_VOL_PATH.end(); vit ++) {
		string curr_dir = *vit;
		struct stat statbuf;
		retstat = lstat(curr_dir.c_str(), &statbuf);
		if (retstat != 0) {
			// Skip those not yet created stores
			retstat = 0;
			continue;
		}
		strcpy(fpath, curr_dir.c_str());
		strncat(fpath, path, PATH_MAX - 1); // ridiculously long paths will break here
		strcpy(fnewpath, curr_dir.c_str());
		strncat(fnewpath, newpath, PATH_MAX - 1); // ridiculously long paths will break here
		retstat = rename(fpath, fnewpath);
		if (retstat < 0) {
			retstat = store_error("store_rename");
			return retstat;
		}
	}
	return retstat;
}

int store_readdir(
		const char *path,
		void *buf,
		fuse_fill_dir_t filler,
		off_t offset,
		struct fuse_file_info *fi,
		map<string, int>& parent_files)
{
	int retstat = 0;

	log_msg(LOG_LEVEL_DEBUG, "\nstore_readdir(path=\"%s\", buf=0x%08x, filler=0x%08x, offset=%lld, fi=0x%08x)\n",
		path, buf, filler, offset, fi);

	vector<string> obj_list;
	objmap_list(path, obj_list, 1);
	objmap_list(path, obj_list, 2);
	vector<string>::iterator vit;
	for(vit=obj_list.begin();vit!=obj_list.end();vit++){
		log_msg(LOG_LEVEL_DEBUG, "    store_readdir filler:  %s", (*vit).c_str());
		if(!parent_files[*vit]) {
			parent_files[*vit] = 1;
			if (filler(buf, (*vit).c_str(), NULL, 0) != 0) {
				log_msg(LOG_LEVEL_ERROR, "    ERROR store_readdir filler:  buffer full");
				return -ENOMEM;
			}
		}
	}

#if 0
	DIR *dp;
	struct dirent *de;
	char fpath[PATH_MAX];

	vector<string>::iterator vit;
	for (vit = STORE_VOL_PATH.begin(); vit != STORE_VOL_PATH.end(); vit ++) {
		string curr_dir = *vit;
		struct stat statbuf;
		retstat = lstat(curr_dir.c_str(), &statbuf);
		if (retstat != 0) {
			// Skip those not yet created stores
			retstat = 0;
			continue;
		}
		strcpy(fpath, curr_dir.c_str());
		strncat(fpath, path, PATH_MAX - 1); // ridiculously long paths will break here
		dp = opendir(fpath);
		log_msg(LOG_LEVEL_DEBUG, "store_readdir calling opendir: %s\n", fpath);
		if (dp == NULL) {
			retstat = store_error("store_readdir opendir");
		}

		// Every directory contains at least two entries: . and ..  If my
		// first call to the system readdir() returns NULL I've got an
		// error; near as I can tell, that's the only condition under
		// which I can get an error from readdir()
		de = readdir(dp);
		if (de == 0) {
			closedir(dp);
			retstat = store_error("store_readdir readdir");
			return retstat;
		}

		// This will copy the entire directory into the buffer.  The loop exits
		// when either the system readdir() returns NULL, or filler()
		// returns something non-zero.  The first case just means I've
		// read the whole directory; the second means the buffer is full.
		do {
			log_msg(LOG_LEVEL_DEBUG, "store_readdir calling filler with name %s, type %d, dirtype %d\n", de->d_name, de->d_type, DT_DIR);
			if(de->d_type == DT_DIR) {
				// When we are crossing the mountpoint, it returns DT_UNKNOWN...
				// Don't add directory, it's all duplicate. Files only.
			} else if (de->d_type == DT_UNKNOWN && parent_files[de->d_name]) {
				// Ignore it too, damn it...
			} else {
				if (filler(buf, de->d_name, NULL, 0) != 0) {
					closedir(dp);
					log_msg(LOG_LEVEL_ERROR, "    ERROR store_readdir filler:  buffer full");
					return -ENOMEM;
				}
			}
		} while ((de = readdir(dp)) != NULL);

		closedir(dp);
	}
#endif

	log_fi(fi);

	return retstat;
}

// Create directories in all data stores
int store_rmdir(const char *path)
{
	int retstat = 0;
	char fpath[PATH_MAX];

	log_msg(LOG_LEVEL_DEBUG, "\nstore_rmdir(path=\"%s\")\n", path);

	vector<string>::iterator vit;
	for (vit = STORE_VOL_PATH.begin(); vit != STORE_VOL_PATH.end(); vit ++) {
		string curr_dir = *vit;
		struct stat statbuf;
		retstat = lstat(curr_dir.c_str(), &statbuf);
		if (retstat != 0) {
			// Skip those not yet created stores
			retstat = 0;
			continue;
		}
		strcpy(fpath, curr_dir.c_str());
		strncat(fpath, path, PATH_MAX - 1); // ridiculously long paths will break here
		retstat = rmdir(fpath);
		if (retstat < 0) {
			retstat = store_error("store_rmdir rmdir");
			return retstat;
		}
	}
	return retstat;
}

int store_migrate(const char *path, const char * from_store, const char * to_store, int keep_source)
{
	int retstat = 0;
	int fd_from = -1;
	int fd_to = -1;
	char fpath_from[PATH_MAX];
	char fpath_to[PATH_MAX];
	struct stat statbuf;

	char * rdbuf = NULL;
	
	int total_bytes_read = 0;
	int total_bytes_written = 0;

	log_msg(LOG_LEVEL_DEBUG, "\nstore_migrate(path \"%s\",from \"%s\", to \"%s\")\n",
		path, from_store, to_store);

	strcpy(fpath_from, from_store);
	strncat(fpath_from, path, PATH_MAX - 1); // ridiculously long paths will break here
	log_msg(LOG_LEVEL_DEBUG, "\nstore_migrate:read(fpath_from\"%s\")\n", fpath_from);

	// fd_from = open(fpath_from, O_DIRECT|O_RDONLY);
	fd_from = open(fpath_from, O_RDONLY);
	if (fd_from < 0) {
		retstat = store_error("store_migrate open for reading");
		return retstat;
	}

	retstat = lstat(fpath_from, &statbuf);
	log_msg(LOG_LEVEL_DEBUG, "source file %s is size of %ld\n", fpath_from, statbuf.st_size);

	strcpy(fpath_to, to_store);
	strncat(fpath_to, path, PATH_MAX - 1); // ridiculously long paths will break here
	log_msg(LOG_LEVEL_DEBUG, "\nstore_migrate:write(fpath_to\"%s\")\n", fpath_to);

	//fd_to = open(fpath_to, O_CREAT|O_WRONLY|O_DIRECT, S_IRUSR | S_IWUSR);
	fd_to = open(fpath_to, O_CREAT|O_WRONLY, S_IRUSR | S_IWUSR);
	if (fd_to < 0) {
		retstat = store_error("store_migrate open for writing");
		return retstat;
	}
	log_msg(LOG_LEVEL_DEBUG, "opened target file %s for writing, with block size of %d\n", fpath_to, DIRECTIO_BLOCK_SIZE);

	int bytes_left = statbuf.st_size;
	if((0 != posix_memalign((void **)&rdbuf, sysconf(_SC_PAGESIZE), DIRECTIO_BLOCK_SIZE)) || (rdbuf == NULL)) {
		close(fd_to);
		close(fd_from);
		printf("Failed to allocate aligned memory for size %d with page size%ld\n", DIRECTIO_BLOCK_SIZE, sysconf(_SC_PAGESIZE));
		return 1;
	}
	log_msg(LOG_LEVEL_DEBUG, "Allocated aligned memory for size %d with page size%ld\n", DIRECTIO_BLOCK_SIZE, sysconf(_SC_PAGESIZE));
	
	while(bytes_left > 0) {
		int bytes_read = read(fd_from, rdbuf, DIRECTIO_BLOCK_SIZE);
		if(bytes_read != -1) total_bytes_read += bytes_read;
		if(bytes_read != DIRECTIO_BLOCK_SIZE) {
			// done reading
			log_msg(LOG_LEVEL_DEBUG, "Done reading, read less than expected: %d vs %d\n", bytes_read, DIRECTIO_BLOCK_SIZE);
			break;
		}

		bytes_left -= DIRECTIO_BLOCK_SIZE;
		int bytes_written = write(fd_to, rdbuf, DIRECTIO_BLOCK_SIZE);
		if(bytes_written != -1) total_bytes_written += bytes_written;
		if(bytes_written != DIRECTIO_BLOCK_SIZE) {
			log_msg(LOG_LEVEL_ERROR, "    ERROR writting %s: %s\n", fpath_to, strerror(errno));
			return 1;
		}
		log_msg(LOG_LEVEL_DEBUG, "Written %d bytes\n", bytes_written);
	}
	
	if(bytes_left > 0) {
		int bytes_written = write(fd_to, rdbuf, bytes_left);
		if(bytes_written != -1) total_bytes_written += bytes_written;
		if(bytes_written != bytes_left) {
			log_msg(LOG_LEVEL_ERROR, "    ERROR writting %s: %s\n", fpath_to, strerror(errno));
			return 1;
		}
		bytes_left -= bytes_written;
		log_msg(LOG_LEVEL_DEBUG, "Written %d bytes\n", bytes_written);
	}

	close(fd_to);
	close(fd_from);
	
	if(rdbuf) free(rdbuf);
	
	log_msg(LOG_LEVEL_DEBUG, "Total bytes read: %d, total bytes written: %d\n", total_bytes_read, total_bytes_written);
	
	if(!keep_source) {
		log_msg(LOG_LEVEL_DEBUG, "\nstore_migrate: remove source file %s\n", fpath_from);
		retstat = unlink(fpath_from);
		if (retstat < 0)
			retstat = store_error("store_migrate unlink");
	}

	return 0;
	
}