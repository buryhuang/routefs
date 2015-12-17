/*
  Kiwi File System
  Copyright (C) 2014 Bury Huang

  This code is derived from function prototypes found /usr/include/fuse/fuse.h
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
  His code is licensed under the LGPLv2.
  A copy of that code is included in the file fuse.h
  
  The point of this FUSE filesystem is to provide an introduction to
  FUSE.  It was my first FUSE filesystem as I got to know the
  software; hopefully, the comments in this code will help people who
  follow later to get a gentler introduction.

  This might be called a no-op filesystem:  it doesn't impose
  filesystem semantics on top of any other existing structure.  It
  simply reports the requests that come in, and passes them to an
  underlying filesystem.  The information is saved in a logfile named
  routefs.log, in the directory from which you run routefs.

  gcc -Wall `pkg-config fuse --cflags --libs` -o routefs routefs.c
*/

#include "params.h"
#include "log.h"
#include "routefs.h"
#include "store.h"
#include "rootmap.h"
#include "objmap.h"
#include "postprocess.h"
#include "ppd.h"
#include "stats.h"

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

#include <vector>

#ifdef DEBUG

#define TIMER_START \
{ \
	struct timespec tS1; \
	tS1.tv_sec = 0; \
	tS1.tv_nsec = 0; \
	clock_gettime(CLOCK_MONOTONIC, &tS1);


#define TIMER_STOP(func) \
	struct timespec tS2; \
	tS2.tv_sec = 0; \
	tS2.tv_nsec = 0; \
	clock_gettime(CLOCK_MONOTONIC, &tS2); \
	log_msg(LOG_LEVEL_ERROR, "%s: %lu ns\n", func, (tS2.tv_sec-tS1.tv_sec)*1000000000 + (tS2.tv_nsec-tS1.tv_nsec)); \
}

class AutoTimer {
public:
	AutoTimer(const char * func): m_func(func)
	{
		m_tS1.tv_sec = 0;
		m_tS1.tv_nsec = 0;
		clock_gettime(CLOCK_MONOTONIC, &m_tS1);
	}
	
	~AutoTimer() {
		m_tS2.tv_sec = 0;
		m_tS2.tv_nsec = 0;
		clock_gettime(CLOCK_MONOTONIC, &m_tS2);
		log_msg(LOG_LEVEL_ERROR, "%s: %lu ns\n", m_func, (m_tS2.tv_sec-m_tS1.tv_sec)*1000000000 + (m_tS2.tv_nsec-m_tS1.tv_nsec));

	}

private:
	const char * m_func;
	struct timespec m_tS1;
	struct timespec m_tS2;

};

#else

#define AutoTimer const char *
#define TIMER_START
#define TIMER_STOP(func)

#endif

#include <string>
#include <vector>
#include <map>

using namespace std;

map<string, string> xattr_map;
std::string default_datadir;

// Report errors to logfile and give -errno to caller
static int ifs_error(const char *str, int log=1)
{
	int ret = -errno;
	if(log) {
		log_msg(LOG_LEVEL_ERROR, "    ERROR %s: %s\n", str, strerror(errno));
	}
	return ret;
}

// Report errors to logfile and give -errno to caller
static int ifs_warn(const char *str, int log=1)
{
	int ret = -errno;
	if(log) {
		log_msg(LOG_LEVEL_WARN, "    WARN %s: %s\n", str, strerror(errno));
	}
	return ret;
}
// Check whether the given user is permitted to perform the given operation on the given 

//  All the paths I see are relative to the root of the mounted
//  filesystem.  In order to get to the underlying filesystem, I need to
//  have the mountpoint.  I'll save it away early on in main(), and then
//  whenever I need a path for something I'll call this to construct
//  it.
static const char * get_suffix(const char * path) {
	int rindex = strlen(path);
	while( rindex-- > 0) {
		if(path[rindex] == '.') {
			return &path[rindex];
		}
	}
	return NULL;
}
/*
 * NULL if no specific type is found
 */
static const string get_realdir(const char * path)
{
	string dest;
	if(!path) return NULL;

	// Check ObjMap first
	string destStr;
	// @todo: it's all hardcoded now
	log_msg(LOG_LEVEL_DEBUG, "get_realdir: found in objmap: path[%s]\n", path);

#ifdef CACHE_MODE
	int ret = objmap_get(path, destStr);
	if(ret != 0) {
		ret = objmap_get(path, destStr, 2); // search L2 if L1 misses
		// Set to promote to cache since it's accessed
	}
#else
	int ret = objmap_get(path, destStr);
#endif
	if(ret == 0) {
		// Found the obj in the objmap db
		log_msg(LOG_LEVEL_DEBUG, "get_realdir: found in objmap: path[%s]=>dest[%s]\n", path, destStr.c_str());
		return destStr.c_str();
	}
	// e.g. rootdir = /store/root/access
	// e.g. realdir = /store/data/L1store/raw
	const char * suffix = get_suffix(path);
	log_msg(LOG_LEVEL_DEBUG, "get_realdir: found in objmap: path[%s]\n", path);
	dest = rootmap_getdest(suffix);

	log_msg(LOG_LEVEL_DEBUG, "get_realdir: path[%s] hint[%s] dest[%s]\n", path, suffix, dest.c_str());
	return dest;
}

// Basically, strip the path off the full path and store it
// @todo: do we really have to do this?
static void ifs_set_objmap(const char *path, const char * dest) {
	string destStr(dest);
	size_t loc = destStr.find(path);
	if(loc != string::npos) {
		log_msg(LOG_LEVEL_DEBUG, "ifs_set_objmap objmap_set: path[%s] store_path[%s]\n", path, destStr.substr(0, loc).c_str());
		objmap_set(path, destStr.substr(0, loc).c_str());
	} else {
		log_msg(LOG_LEVEL_DEBUG, "ifs_set_objmap invalid input, cannot find past in dest: path[%s] dest[%s]\n", path, dest);
	}
}

static void ifs_fullpath(char fpath[PATH_MAX], const char *path)
{
	AutoTimer _timer(__FUNCTION__);

	strcpy(fpath, IFS_DATA->rootdir);
	strncat(fpath, path, PATH_MAX - 1); // ridiculously long paths will break here

	// @todo: Temp Disabled!
	string realdir;

	realdir = get_realdir(path);
	if(realdir.length()) {
		strcpy(fpath, realdir.c_str());
		strncat(fpath, path, PATH_MAX - 1); // ridiculously long paths will break here
	}

	log_msg(LOG_LEVEL_DEBUG, "    ifs_fullpath:  rootdir = \"%s\", realdir = \"%s\", path = \"%s\", fpath = \"%s\"\n",
		IFS_DATA->rootdir, realdir.c_str(), path, fpath);
}

static void ifs_fullpath_root(char fpath[PATH_MAX], const char *path)
{
	AutoTimer _timer(__FUNCTION__);

	strcpy(fpath, IFS_DATA->rootdir);
	strncat(fpath, path, PATH_MAX - 1); // ridiculously long paths will break here

	log_msg(LOG_LEVEL_DEBUG, "    ifs_fullpath_root:  rootdir = \"%s\", path = \"%s\", fpath = \"%s\"\n",
		IFS_DATA->rootdir, path, fpath);
}

///////////////////////////////////////////////////////////
//
// Prototypes for all these functions, and the C-style comments,
// come indirectly from /usr/include/fuse.h
//
/** Get file attributes.
 *
 * Similar to stat().  The 'st_dev' and 'st_blksize' fields are
 * ignored.  The 'st_ino' field is ignored except if the 'use_ino'
 * mount option is given.
 */
int ifs_getattr(const char *path, struct stat *statbuf)
{
	AutoTimer _timer(__FUNCTION__);

	int retstat = 0;
	char fpath[PATH_MAX];

	log_msg(LOG_LEVEL_DEBUG, "\nifs_getattr(path=\"%s\", statbuf=0x%08x)\n", path, statbuf);
	ifs_fullpath(fpath, path);

	retstat = lstat(fpath, statbuf);
	if (retstat != 0) {
		// This function is used to check file existance
		// Key performance function, get rid of unneccessary performance
		retstat = ifs_error("ifs_getattr lstat", 0); // no log
		// log_stat(statbuf);
	}

	return retstat;
}

/** Read the target of a symbolic link
 *
 * The buffer should be filled with a null terminated string.  The
 * buffer size argument includes the space for the terminating
 * null character.  If the linkname is too long to fit in the
 * buffer, it should be truncated.  The return value should be 0
 * for success.
 */
// Note the system readlink() will truncate and lose the terminating
// null.  So, the size passed to to the system readlink() must be one
// less than the size passed to ifs_readlink()
// ifs_readlink() code by Bernardo F Costa (thanks!)
int ifs_readlink(const char *path, char *link, size_t size)
{
	AutoTimer _timer(__FUNCTION__);

    int retstat = 0;
    char fpath[PATH_MAX];
    
    log_msg(LOG_LEVEL_DEBUG, "ifs_readlink(path=\"%s\", link=\"%s\", size=%d)\n",
	  path, link, size);
    ifs_fullpath(fpath, path);
    
    retstat = readlink(fpath, link, size - 1);
    if (retstat < 0)
	retstat = ifs_error("ifs_readlink readlink");
    else  {
	link[retstat] = '\0';
	retstat = 0;
    }
    
    return retstat;
}

/** Create a file node
 *
 * There is no create() operation, mknod() will be called for
 * creation of all non-directory, non-symlink nodes.
 */
// shouldn't that comment be "if" there is no.... ?

int ifs_mknod(const char *path, mode_t mode, dev_t dev)
{
	AutoTimer _timer(__FUNCTION__);

	int retstat = 0;
	int fd = -1;
	char fpath[PATH_MAX];

	log_msg(LOG_LEVEL_DEBUG, "\nifs_mknod(path=\"%s\", mode=0%3o, dev=%lld)\n",
		path, mode, dev);
	ifs_fullpath(fpath, path);
	ifs_set_objmap(path, fpath);

	// On Linux this could just be 'mknod(path, mode, rdev)' but this
	//  is more portable
	if (S_ISREG(mode)) {
		fd = open(fpath, O_CREAT | O_EXCL | O_WRONLY, mode);
		if (fd < 0) {
			retstat = ifs_error("ifs_mknod open");
		} else {
			retstat = close(fd);
			if (retstat < 0)
				retstat = ifs_error("ifs_mknod close");
		}
	} else if (S_ISFIFO(mode)) {
		retstat = mkfifo(fpath, mode);
		if (retstat < 0)
			retstat = ifs_error("ifs_mknod mkfifo");
	} else {
		retstat = mknod(fpath, mode, dev);
		if (retstat < 0)
			retstat = ifs_error("ifs_mknod mknod");
	}
	
	return retstat;
}

/** Create a directory */
int ifs_mkdir(const char *path, mode_t mode)
{
	AutoTimer _timer(__FUNCTION__);

	int retstat = 0;
	char fpath[PATH_MAX];

	log_msg(LOG_LEVEL_DEBUG, "\nifs_mkdir(path=\"%s\", mode=0%3o)\n", path, mode);
	ifs_fullpath_root(fpath, path);

	// @todo: Temp Disabled!
	retstat = store_mkdir(path, mode);
	if (retstat < 0) {
		retstat = ifs_error("ifs_mkdir store_mkdir");
		return retstat;
	}

	retstat = mkdir(fpath, mode);
	if (retstat < 0) {
		retstat = ifs_error("ifs_mkdir mkdir");
		return retstat;
	}

	return 0;
}

/** Remove a file */
int ifs_unlink(const char *path)
{
	AutoTimer _timer(__FUNCTION__);

	int retstat1 = 0;
	int retstat2 = 0;
	int retstat  = 0;
	char fpath[PATH_MAX];

	log_msg(LOG_LEVEL_DEBUG, "ifs_unlink(path=\"%s\")\n",
		path);

	ifs_fullpath(fpath, path);
	retstat1 = unlink(fpath);
	if (retstat1 < 0) {
		retstat1 = ifs_warn("ifs_unlink unlink no such file in L1");
	}
	objmap_del(path);
	stats_del(path);

	#ifdef CACHE_MODE
	// Tricky, in cached mode
	// it could have 2 copies, this is the second copy
	ifs_fullpath(fpath, path);
	retstat2 = unlink(fpath);
	if (retstat2 < 0) {
		ifs_warn("ifs_unlink no such file in L2");
		// retstat = ifs_error("ifs_unlink unlink");
	}
	objmap_del(path, 2);
	//stats_del(path); // Intentional, no need to delete, stats is for L1
	#endif
	
	if(retstat1 < 0 && retstat2 < 0) {
		retstat = retstat1;
	}

	return retstat;
}

/** Remove a directory */
int ifs_rmdir(const char *path)
{
	AutoTimer _timer(__FUNCTION__);

	int retstat = 0;
	char fpath[PATH_MAX];

	log_msg(LOG_LEVEL_DEBUG, "ifs_rmdir(path=\"%s\")\n", path);

	retstat = store_rmdir(path);
	if (retstat < 0) {
		retstat = ifs_error("ifs_rmdir store_rmdir");
		return retstat;
	}

	ifs_fullpath_root(fpath, path);
	retstat = rmdir(fpath);
	if (retstat < 0) {
		retstat = ifs_error("ifs_rmdir rmdir");
	}

	return retstat;
}

/** Create a symbolic link */
// The parameters here are a little bit confusing, but do correspond
// to the symlink() system call.  The 'path' is where the link points,
// while the 'link' is the link itself.  So we need to leave the path
// unaltered, but insert the link into the mounted directory.
int ifs_symlink(const char *path, const char *link)
{
	AutoTimer _timer(__FUNCTION__);

    int retstat = 0;
    char flink[PATH_MAX];
    
    log_msg(LOG_LEVEL_DEBUG, "\nifs_symlink(path=\"%s\", link=\"%s\")\n",
	    path, link);
    ifs_fullpath(flink, link);
	ifs_set_objmap(link, flink);

    retstat = symlink(path, flink);
    if (retstat < 0)
	retstat = ifs_error("ifs_symlink symlink");
    
    return retstat;
}

/** Rename a file */
// both path and newpath are fs-relative
int ifs_rename(const char *path, const char *newpath)
{
	AutoTimer _timer(__FUNCTION__);

	int retstat = 0;
	char fpath[PATH_MAX];
	char fnewpath[PATH_MAX];
	int path_is_dir = 0;

	log_msg(LOG_LEVEL_DEBUG, "\nifs_rename(path=\"%s\", newpath=\"%s\")\n",
		path, newpath);

	ifs_fullpath_root(fpath, path);
	ifs_fullpath_root(fnewpath, newpath);

	// Only DIR need to travers all the way down the tree
	// @todo: should this logic be put here?
	struct stat statbuf;
	retstat = lstat(fpath, &statbuf);

	log_msg(LOG_LEVEL_DEBUG, "\nifs_rename(fpath=\"%s\", st_mode=%d)\n", fpath, statbuf.st_mode);
	if(retstat != -1 && S_ISDIR(statbuf.st_mode)) {
		path_is_dir = 1;
		log_msg(LOG_LEVEL_DEBUG, "\nifs_rename:dir(fpath=\"%s\", fnewpath=\"%s\")\n",
			fpath, fnewpath);
		retstat = store_rename(path, newpath);
		if (retstat < 0) {
			retstat = ifs_error("ifs_rename store_rename");
			return retstat;
		}
	} else {
		// Then this is a non-dir file, need to get the real
		// destination
		ifs_fullpath(fpath, path);
		ifs_fullpath(fnewpath, newpath);
		log_msg(LOG_LEVEL_DEBUG, "\nifs_rename:regular(fpath=\"%s\", fnewpath=\"%s\")\n",
			fpath, fnewpath);
	}

	// Only after all store path is update can the root be updated.
	retstat = rename(fpath, fnewpath);

	if (retstat < 0) {
		retstat = ifs_error("ifs_rename rename");
	} else if (!path_is_dir) {
		// We don't keep folder in objmap
		// @todo:change of store in objmap should be handled by the post-processing code
		//
		// Assumption:
		// In renaming, all input pathes are relative paths
		// The objmap stores the <store_path, full_relative_path>
		// so the store_path never is going to change
		// The only "risk" is when we are doing post-processing
		// The store_path may change, that should be handled by
		// the post-processing code
		string store_path;
		int ret = objmap_get(path, store_path);
		if(ret != -1) {
			// Update the database only after rename is successful
			objmap_set(newpath, store_path.c_str());
			// Only delete old entries if newpath is recorded
			objmap_del(path);
			#ifdef CACHE_MODE
			objmap_del(path, 2);
			#endif
		} else {
			retstat = ifs_error("ifs_rename rename: cannot find obj in objmap");
		}
	}

	return retstat;
}

/** Create a hard link to a file */
int ifs_link(const char *path, const char *newpath)
{
	AutoTimer _timer(__FUNCTION__);

    int retstat = 0;
    char fpath[PATH_MAX], fnewpath[PATH_MAX];
    
    log_msg(LOG_LEVEL_DEBUG, "\nifs_link(path=\"%s\", newpath=\"%s\")\n",
	    path, newpath);
    ifs_fullpath(fpath, path);
    ifs_fullpath(fnewpath, newpath);
    
    retstat = link(fpath, fnewpath);
    if (retstat < 0)
	retstat = ifs_error("ifs_link link");
    
    return retstat;
}

/** Change the permission bits of a file */
int ifs_chmod(const char *path, mode_t mode)
{
	AutoTimer _timer(__FUNCTION__);

    int retstat = 0;
    char fpath[PATH_MAX];
    
    log_msg(LOG_LEVEL_DEBUG, "\nifs_chmod(fpath=\"%s\", mode=0%03o)\n",
	    path, mode);
    ifs_fullpath(fpath, path);
    
    retstat = chmod(fpath, mode);
    if (retstat < 0)
	retstat = ifs_error("ifs_chmod chmod");
    
    return retstat;
}

/** Change the owner and group of a file */
int ifs_chown(const char *path, uid_t uid, gid_t gid)
  
{
	AutoTimer _timer(__FUNCTION__);

    int retstat = 0;
    char fpath[PATH_MAX];
    
    log_msg(LOG_LEVEL_DEBUG, "\nifs_chown(path=\"%s\", uid=%d, gid=%d)\n",
	    path, uid, gid);
    ifs_fullpath(fpath, path);
    
    retstat = chown(fpath, uid, gid);
    if (retstat < 0)
		retstat = ifs_warn("ifs_chown chown");
    
    // @todo: ignore chown error since we are fuse...
    return 0;
    //return retstat;
}

/** Change the size of a file */
int ifs_truncate(const char *path, off_t newsize)
{
	AutoTimer _timer(__FUNCTION__);

    int retstat = 0;
    char fpath[PATH_MAX];
    
    log_msg(LOG_LEVEL_DEBUG, "\nifs_truncate(path=\"%s\", newsize=%lld)\n",
	    path, newsize);
    ifs_fullpath(fpath, path);
    
    retstat = truncate(fpath, newsize);
    if (retstat < 0)
	ifs_error("ifs_truncate truncate");
    
    return retstat;
}

int ifs_utimens(const char * path, const struct timespec tv[2])
{
	AutoTimer _timer(__FUNCTION__);

	int retstat = 0;
	char fpath[PATH_MAX];
	
	log_msg(LOG_LEVEL_DEBUG, "\nifs_utimens(path=\"%s\", ubuf=0x%08x)\n",
		path, tv);
	ifs_fullpath(fpath, path);
	
	retstat = utimensat(0, fpath, tv, AT_SYMLINK_NOFOLLOW); // use absolute path, ignore dirfd
	if (retstat < 0)
		retstat = ifs_warn("ifs_utimens utimensat");
	
	// @todo: ignore the result, as best effort, we are fuse, not root
	return 0;
	// return retstat;
}


/** Change the access and/or modification times of a file */
/* note -- I'll want to change this as soon as 2.6 is in debian testing */
int ifs_utime(const char *path, struct utimbuf *ubuf)
{
	AutoTimer _timer(__FUNCTION__);

	int retstat = 0;
	char fpath[PATH_MAX];
	
	log_msg(LOG_LEVEL_DEBUG, "\nifs_utime(path=\"%s\", ubuf=0x%08x)\n",
		path, ubuf);
	ifs_fullpath(fpath, path);
	
	log_msg(LOG_LEVEL_DEBUG, "\nutime(fpath=\"%s\")\n", fpath);
	retstat = utime(fpath, ubuf);
	if (retstat < 0)
		retstat = ifs_warn("ifs_utime utime");
	
	return retstat;
}

/** File open operation
 *
 * No creation, or truncation flags (O_CREAT, O_EXCL, O_TRUNC)
 * will be passed to open().  Open should check if the operation
 * is permitted for the given flags.  Optionally open may also
 * return an arbitrary filehandle in the fuse_file_info structure,
 * which will be passed to all file operations.
 *
 * Changed in version 2.2
 */
int ifs_open(const char *path, struct fuse_file_info *fi)
{
	AutoTimer _timer(__FUNCTION__);

	int retstat = 0;
	int fd = -1;
	char fpath[PATH_MAX];

	log_msg(LOG_LEVEL_DEBUG, "\nifs_open(path\"%s\", fi=0x%08x)\n",
		path, fi);
	ifs_fullpath(fpath, path);

	fd = open(fpath, fi->flags);
	if (fd < 0)
		retstat = ifs_error("ifs_open open");
		
	// Update the stats records
	char timestamp[128];
	snprintf(timestamp, 128, "%d",(unsigned)time(NULL));
	stats_set(path, timestamp);
	// @todo: right, not just from target to source
	postprocess_set(path, 0, STORE_DATA_STAGING_TARGET.store_name, STORE_DATA_STAGING_SOURCE.store_name);

	fi->fh = fd;

	log_fi(fi);

	return retstat;
}

/** Read data from an open file
 *
 * Read should return exactly the number of bytes requested except
 * on EOF or error, otherwise the rest of the data will be
 * substituted with zeroes.  An exception to this is when the
 * 'direct_io' mount option is specified, in which case the return
 * value of the read system call will reflect the return value of
 * this operation.
 *
 * Changed in version 2.2
 */
// I don't fully understand the documentation above -- it doesn't
// match the documentation for the read() system call which says it
// can return with anything up to the amount of data requested. nor
// with the fusexmp code which returns the amount of data also
// returned by read.
int ifs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	AutoTimer _timer(__FUNCTION__);

	int retstat = 0;
	int bytes_read = 0;
	
	log_msg(LOG_LEVEL_DEBUG, "\nifs_read(path=\"%s\", buf=0x%08x, size=%d, offset=%lld, fi=0x%08x)\n",
		path, buf, size, offset, fi);
	// no need to get fpath on this one, since I work from fi->fh not the path
	log_fi(fi);

	bytes_read = pread(fi->fh, buf, size, offset);
	if (retstat < 0) {
		bytes_read = -1;
		retstat = ifs_error("ifs_read read");
	}

	return bytes_read;
}

/** Write data to an open file
 *
 * Write should return exactly the number of bytes requested
 * except on error.  An exception to this is when the 'direct_io'
 * mount option is specified (see read operation).
 *
 * Changed in version 2.2
 */
// As  with read(), the documentation above is inconsistent with the
// documentation for the write() system call.

int ifs_write(const char *path, const char *buf, size_t size, off_t offset,
		struct fuse_file_info *fi)
{
	AutoTimer _timer(__FUNCTION__);

	int retstat = 0;
	int bytes_written = 0;

	log_msg(LOG_LEVEL_DEBUG, "\nifs_write(path=\"%s\", buf=0x%08x, size=%d, offset=%lld, fi=0x%08x)\n",
		path, buf, size, offset, fi);
	// no need to get fpath on this one, since I work from fi->fh not the path
	// log_fi(fi);

	//log_msg(LOG_LEVEL_DEBUG, "\nifs_write(path=\"%s\") writing master data buf=%p size=%d offset=%d\n",
	//	path, buf, size, offset);
	bytes_written = pwrite(fi->fh, buf, size, offset);

	if (retstat < 0) {
		log_fi(fi);
		retstat = ifs_error("ifs_write pwrite");
		bytes_written = -1;
	}

	return bytes_written;
}

/** Get file system statistics
 *
 * The 'f_frsize', 'f_favail', 'f_fsid' and 'f_flag' fields are ignored
 *
 * Replaced 'struct statfs' parameter with 'struct statvfs' in
 * version 2.5
 */
int ifs_statfs(const char *path, struct statvfs *statv)
{
	AutoTimer _timer(__FUNCTION__);

	int retstat = 0;
	char fpath[PATH_MAX];

	log_msg(LOG_LEVEL_DEBUG, "\nifs_statfs(path=\"%s\", statv=0x%08x)\n",
		path, statv);
	ifs_fullpath(fpath, path);

	// get stats for underlying filesystem
	retstat = statvfs(fpath, statv);
	if (retstat < 0)
	retstat = ifs_error("ifs_statfs statvfs");

	log_statvfs(statv);

	return retstat;
}

/** Possibly flush cached data
 *
 * BIG NOTE: This is not equivalent to fsync().  It's not a
 * request to sync dirty data.
 *
 * Flush is called on each close() of a file descriptor.  So if a
 * filesystem wants to return write errors in close() and the file
 * has cached dirty data, this is a good place to write back data
 * and return any errors.  Since many applications ignore close()
 * errors this is not always useful.
 *
 * NOTE: The flush() method may be called more than once for each
 * open().  This happens if more than one file descriptor refers
 * to an opened file due to dup(), dup2() or fork() calls.  It is
 * not possible to determine if a flush is final, so each flush
 * should be treated equally.  Multiple write-flush sequences are
 * relatively rare, so this shouldn't be a problem.
 *
 * Filesystems shouldn't assume that flush will always be called
 * after some writes, or that if will be called at all.
 *
 * Changed in version 2.2
 */
int ifs_flush(const char *path, struct fuse_file_info *fi)
{
	AutoTimer _timer(__FUNCTION__);

	int retstat = 0;

	log_msg(LOG_LEVEL_DEBUG, "\nifs_flush(path=\"%s\", fi=0x%08x)\n", path, fi);
	// no need to get fpath on this one, since I work from fi->fh not the path
	log_fi(fi);

	return retstat;
}

/** Release an open file
 *
 * Release is called when there are no more references to an open
 * file: all file descriptors are closed and all memory mappings
 * are unmapped.
 *
 * For every open() call there will be exactly one release() call
 * with the same flags and file descriptor.  It is possible to
 * have a file opened more than once, in which case only the last
 * release will mean, that no more reads/writes will happen on the
 * file.  The return value of release is ignored.
 *
 * Changed in version 2.2
 */
int ifs_release(const char *path, struct fuse_file_info *fi)
{
	AutoTimer _timer(__FUNCTION__);

	int retstat = 0;

	log_msg(LOG_LEVEL_DEBUG, "\nifs_release(path=\"%s\", fi=0x%08x)\n", path, fi);
	log_fi(fi);

	// We need to close the file.  Had we allocated any resources
	// (buffers etc) we'd need to free them here as well.
	retstat = close(fi->fh);

	// Only migrate on creation time
	if(~(fi->flags) & O_CREAT) {
		// Add to the post-processing queue
		char fpath[PATH_MAX];
		ifs_fullpath_root(fpath, path);
	
		// @todo: post-processing should happen POST!
		// Here is only a hack to make it in-line to see the effect
		// and avoid the problem of objmap(database) locking for now
	
		// Only DIR need to travers all the way down the tree
		// @todo: should this logic be put here?
		struct stat statbuf;
		retstat = lstat(fpath, &statbuf);
	
		log_msg(LOG_LEVEL_DEBUG, "\nifs_release(fpath=\"%s\", st_mode=%d)\n", fpath, statbuf.st_mode);
		if(retstat != -1 && S_ISDIR(statbuf.st_mode)) {
			log_msg(LOG_LEVEL_DEBUG, "\nifs_release:dir(fpath=\"%s\"), skipping post-processing\n", fpath);
		} else {
			string store_path;

			retstat = 0;
			// Then this is a non-dir file, need to get the real
			// destination
			ifs_fullpath(fpath, path);
			int ret = objmap_get(path, store_path);
			if(ret != -1) {
				postprocess_set(path, 0, store_path.c_str());
			}

		}	
	}


	return retstat;
}

/** Synchronize file contents
 *
 * If the datasync parameter is non-zero, then only the user data
 * should be flushed, not the meta data.
 *
 * Changed in version 2.2
 */
int ifs_fsync(const char *path, int datasync, struct fuse_file_info *fi)
{
	AutoTimer _timer(__FUNCTION__);

    int retstat = 0;
    
    log_msg(LOG_LEVEL_DEBUG, "\nifs_fsync(path=\"%s\", datasync=%d, fi=0x%08x)\n",
	    path, datasync, fi);
    log_fi(fi);
    
    if (datasync)
	retstat = fdatasync(fi->fh);
    else
	retstat = fsync(fi->fh);
    
    if (retstat < 0)
	ifs_error("ifs_fsync fsync");
    
    return retstat;
}

/** Set extended attributes */
int ifs_setxattr(const char *path, const char *name, const char *value, size_t size, int flags)
{
	AutoTimer _timer(__FUNCTION__);

	int retstat = 0;
	char fpath[PATH_MAX];

	log_msg(LOG_LEVEL_DEBUG, "\nifs_setxattr(path=\"%s\", name=\"%s\", value=\"%s\", size=%d, flags=0x%08x)\n",
		path, name, value, size, flags);

	if( path == NULL || name == NULL || value == NULL || size == 0) return -1;
	
	ifs_fullpath(fpath, path);

	xattr_map[name] = value;

/*
	retstat = lsetxattr(fpath, name, value, size, flags);
	if (retstat < 0)
		retstat = ifs_error("ifs_setxattr lsetxattr");
*/
	return retstat;
}

/** Get extended attributes */
int ifs_getxattr(const char *path, const char *name, char *value, size_t size)
{
	AutoTimer _timer(__FUNCTION__);

	int retstat = 0;
	char fpath[PATH_MAX];

	log_msg(LOG_LEVEL_DEBUG, "\nifs_getxattr(path = \"%s\", name = \"%s\", value = 0x%08x, size = %d)\n",
		path, name, value, size);
	
	if( name == NULL || value == NULL || size == 0) return 0;
	
	ifs_fullpath(fpath, path);

	if(xattr_map.find(name) != xattr_map.end()) {
		string attr_val = xattr_map[name];
		snprintf(value, size, "%s", attr_val.c_str());
		retstat = attr_val.size();
		log_msg(LOG_LEVEL_DEBUG, "    value = \"%s\"\n", value);
	} else {
		string user_attr = name;
		int user_pos = user_attr.find("user.");
		if( user_pos == 0) {
			user_attr = user_attr.substr(user_pos + strlen("user."));

			if (xattr_map.find(user_attr) != xattr_map.end()) {
				string attr_val = xattr_map[user_attr];
				snprintf(value, size, "%s", attr_val.c_str());
				retstat = attr_val.size();
				log_msg(LOG_LEVEL_DEBUG, "    user value = \"%s\"\n", value);
			}
		}
	}

	if(retstat == 0) {
		retstat = lgetxattr(fpath, name, value, size);
		if (retstat < 0)
			retstat = ifs_error("ifs_getxattr lgetxattr");
		else
			log_msg(LOG_LEVEL_DEBUG, "    value = \"%s\"\n", value);
	}

	return retstat;
}

/** List extended attributes */
int ifs_listxattr(const char *path, char *list, size_t size)
{
	AutoTimer _timer(__FUNCTION__);

	int retstat = 0;
	char fpath[PATH_MAX];
	char *ptr;

	log_msg(LOG_LEVEL_DEBUG, "ifs_listxattr(path=\"%s\", list=0x%08x, size=%d)\n",
		path, list, size
		);
		
	if( list == NULL || size == 0) return 0;

	ifs_fullpath(fpath, path);
	
	memset(list, 0, size);

	size_t curr_pos = 0;
	map<string, string>::iterator mit = xattr_map.begin();
	for(;mit != xattr_map.end();mit++) {
		size_t curr_size = (mit->first).size();
		snprintf(list + curr_pos, curr_size + 1, "%s", (mit->first).c_str());
		curr_pos += curr_size + 1; // keep the '\0' as seperator
		if(curr_pos >= size) break;
	}
	retstat = curr_pos;

/*
	retstat = llistxattr(fpath, list, size);
	if (retstat < 0)
		retstat = ifs_error("ifs_listxattr llistxattr");
*/

	log_msg(LOG_LEVEL_DEBUG, "    returned attributes (length %d):\n", retstat);
	for (ptr = list; ptr < list + retstat; ptr += strlen(ptr)+1)
		log_msg(LOG_LEVEL_DEBUG, "    \"%s\"\n", ptr);

	return retstat;
}

/** Remove extended attributes */
int ifs_removexattr(const char *path, const char *name)
{
	AutoTimer _timer(__FUNCTION__);

    int retstat = 0;
    char fpath[PATH_MAX];
    
    log_msg(LOG_LEVEL_DEBUG, "\nifs_removexattr(path=\"%s\", name=\"%s\")\n",
	    path, name);
    ifs_fullpath(fpath, path);
    
    retstat = lremovexattr(fpath, name);
    if (retstat < 0)
	retstat = ifs_error("ifs_removexattr lrmovexattr");
    
    return retstat;
}

/** Open directory
 *
 * This method should check if the open operation is permitted for
 * this  directory
 *
 * Introduced in version 2.3
 */
int ifs_opendir(const char *path, struct fuse_file_info *fi)
{
	AutoTimer _timer(__FUNCTION__);

	DIR *dp;
	int retstat = 0;
	char fpath[PATH_MAX];

	log_msg(LOG_LEVEL_DEBUG, "\nifs_opendir(path=\"%s\", fi=0x%08x)\n", path, fi);
	ifs_fullpath_root(fpath, path);

	dp = opendir(fpath);
	if (dp == NULL) {
		retstat = ifs_error("ifs_opendir opendir");
	}

	fi->fh = (intptr_t) dp;

	log_fi(fi);

	return retstat;
}

/** Read directory
 *
 * This supersedes the old getdir() interface.  New applications
 * should use this.
 *
 * The filesystem may choose between two modes of operation:
 *
 * 1) The readdir implementation ignores the offset parameter, and
 * passes zero to the filler function's offset.  The filler
 * function will not return '1' (unless an error happens), so the
 * whole directory is read in a single readdir operation.  This
 * works just like the old getdir() method.
 *
 * 2) The readdir implementation keeps track of the offsets of the
 * directory entries.  It uses the offset parameter and always
 * passes non-zero offset to the filler function.  When the buffer
 * is full (or an error happens) the filler function will return
 * '1'.
 *
 * Introduced in version 2.3
 */
int ifs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset,
	       struct fuse_file_info *fi)
{
	AutoTimer _timer(__FUNCTION__);

	int retstat = 0;
	DIR *dp;
	struct dirent *de;
	
	// @todo: I know...this is slow and memory consuming and not scalable...
	map<string, int> parent_files;

	log_msg(LOG_LEVEL_DEBUG, "\nifs_readdir(path=\"%s\", buf=0x%08x, filler=0x%08x, offset=%lld, fi=0x%08x)\n",
		path, buf, filler, offset, fi);

	// once again, no need for fullpath -- but note that I need to cast fi->fh
	dp = (DIR *) (uintptr_t) fi->fh;

	// Every directory contains at least two entries: . and ..  If my
	// first call to the system readdir() returns NULL I've got an
	// error; near as I can tell, that's the only condition under
	// which I can get an error from readdir()
	de = readdir(dp);
	if (de == 0) {
		retstat = ifs_error("ifs_readdir readdir");
		return retstat;
	}

	// This will copy the entire directory into the buffer.  The loop exits
	// when either the system readdir() returns NULL, or filler()
	// returns something non-zero.  The first case just means I've
	// read the whole directory; the second means the buffer is full.
	do {
		log_msg(LOG_LEVEL_DEBUG, "calling filler with name %s\n", de->d_name);
		parent_files[de->d_name] = 1;
		if (filler(buf, de->d_name, NULL, 0) != 0) {
			log_msg(LOG_LEVEL_ERROR, "    ERROR ifs_readdir filler:  buffer full");
			return -ENOMEM;
		}
	} while ((de = readdir(dp)) != NULL);

	// Read the store list second, the order IS important
	retstat = store_readdir( path, buf, filler, offset, fi, parent_files);
	if (retstat < 0) {
		retstat = ifs_error("ifs_readdir store_readdir");
		return retstat;
	}

	log_fi(fi);

	return retstat;
}

/** Release directory
 *
 * Introduced in version 2.3
 */
int ifs_releasedir(const char *path, struct fuse_file_info *fi)
{
	AutoTimer _timer(__FUNCTION__);

    int retstat = 0;
    
    log_msg(LOG_LEVEL_DEBUG, "\nifs_releasedir(path=\"%s\", fi=0x%08x)\n",
	    path, fi);
    log_fi(fi);
    
    closedir((DIR *) (uintptr_t) fi->fh);
    
    return retstat;
}

/** Synchronize directory contents
 *
 * If the datasync parameter is non-zero, then only the user data
 * should be flushed, not the meta data
 *
 * Introduced in version 2.3
 */
// when exactly is this called?  when a user calls fsync and it
// happens to be a directory? ???
int ifs_fsyncdir(const char *path, int datasync, struct fuse_file_info *fi)
{
	AutoTimer _timer(__FUNCTION__);

    int retstat = 0;
    
    log_msg(LOG_LEVEL_DEBUG, "\nifs_fsyncdir(path=\"%s\", datasync=%d, fi=0x%08x)\n",
	    path, datasync, fi);
    log_fi(fi);
    
    return retstat;
}

/**
 * Initialize filesystem
 *
 * The return value will passed in the private_data field of
 * fuse_context to all file operations and as a parameter to the
 * destroy() method.
 *
 * Introduced in version 2.3
 * Changed in version 2.6
 */
// Undocumented but extraordinarily useful fact:  the fuse_context is
// set up before this function is called, and
// fuse_get_context()->private_data returns the user_data passed to
// fuse_main().  Really seems like either it should be a third
// parameter coming in here, or else the fact should be documented
// (and this might as well return void, as it did in older versions of
// FUSE).
void *ifs_init(struct fuse_conn_info *conn)
{
	AutoTimer _timer(__FUNCTION__);

	log_msg(LOG_LEVEL_DEBUG, "\nifs_init()\n");

	int status = 0;

	/*
	 * Set current mount point info
	 */
	//ifs_setxattr("/", "fss_data_out", IFS_DATA->rootdir, PATH_MAX, 0);
	

	// Initialize the type map
	status = rootmap_init(IFS_DATA->rootdir, default_datadir.c_str());
	if (0 != status) {
		log_msg(LOG_LEVEL_ERROR, "Failed to initialize rootmap\n");
		return IFS_DATA;
	}

	// Save the type string to be visible for users
	//string type_str = rootmap_gettype_str();
	//ifs_setxattr("/", "fss_typeval", type_str.c_str(), PATH_MAX, 0);

	// Save the map string to be visible for users
	//string map_str = rootmap_getmap_str();
	//ifs_setxattr("/", "fss_rootmap", map_str.c_str(), PATH_MAX, 0);
	
	// Initialize the obj map
	status = objmap_init();
	if (0 != status) {
		log_msg(LOG_LEVEL_ERROR, "Failed to initialize objmap\n");
		return IFS_DATA;
	}
	
	// Initialize the post-processing db
	status = postprocess_init();
	if (0 != status) {
		log_msg(LOG_LEVEL_ERROR, "Failed to initialize postprocess\n");
		return IFS_DATA;
	}

	// Initialize the stats db
	status = stats_init();
	if (0 != status) {
		log_msg(LOG_LEVEL_ERROR, "Failed to initialize stats\n");
		return IFS_DATA;
	}

	// Initialize the post-process queue thread
	ppd_thread_start();

	snprintf(IFS_DATA->rootdir,  PATH_MAX, "%s", default_datadir.c_str());

	return IFS_DATA;
}

/**
 * Clean up filesystem
 *
 * Called on filesystem exit.
 *
 * Introduced in version 2.3
 */
void ifs_destroy(void *userdata)
{
	AutoTimer _timer(__FUNCTION__);

	log_msg(LOG_LEVEL_DEBUG, "\nifs_destroy(userdata=0x%08x)\n", userdata);
}

/**
 * Check file access permissions
 *
 * This will be called for the access() system call.  If the
 * 'default_permissions' mount option is given, this method is not
 * called.
 *
 * This method is not called under Linux kernel versions 2.4.x
 *
 * Introduced in version 2.5
 */
int ifs_access(const char *path, int mask)
{
	AutoTimer _timer(__FUNCTION__);

	int retstat = 0;
	char fpath[PATH_MAX];
	
	log_msg(LOG_LEVEL_DEBUG, "\nifs_access(path=\"%s\", mask=0%o)\n",
	    path, mask);
	ifs_fullpath(fpath, path);
	
	retstat = access(fpath, mask);
	
	if (retstat < 0)
	retstat = ifs_error("ifs_access access");
	
	return retstat;
}

/**
 * Create and open a file
 *
 * If the file does not exist, first create it with the specified
 * mode, and then open it.
 *
 * If this method is not implemented or under Linux kernel
 * versions earlier than 2.6.15, the mknod() and open() methods
 * will be called instead.
 *
 * Introduced in version 2.5
 */
int ifs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	AutoTimer _timer(__FUNCTION__);

	int retstat = 0;
	char fpath[PATH_MAX];
	int fd = -1;

	log_msg(LOG_LEVEL_DEBUG, "\nifs_create(path=\"%s\", mode=0%03o, fi=0x%08x)\n",
		path, mode, fi);
	ifs_fullpath(fpath, path);

	// @todo: Temp Disabled!
	ifs_set_objmap(path, fpath);

	fd = creat(fpath, mode); // Force failure if file exists!
	if (fd < 0)
		retstat = ifs_error("ifs_create creat");
		
	fi->fh = fd;

    return retstat;
}

/**
 * Change the size of an open file
 *
 * This method is called instead of the truncate() method if the
 * truncation was invoked from an ftruncate() system call.
 *
 * If this method is not implemented or under Linux kernel
 * versions earlier than 2.6.15, the truncate() method will be
 * called instead.
 *
 * Introduced in version 2.5
 */
int ifs_ftruncate(const char *path, off_t offset, struct fuse_file_info *fi)
{
	AutoTimer _timer(__FUNCTION__);

	int retstat = 0;

	log_msg(LOG_LEVEL_DEBUG, "\nifs_ftruncate(path=\"%s\", offset=%lld, fi=0x%08x)\n",
		path, offset, fi);
	log_fi(fi);

	retstat = ftruncate(fi->fh, offset);
	if (retstat < 0) {
		retstat = ifs_error("ifs_ftruncate ftruncate");
	}

	return retstat;
}

/**
 * Get attributes from an open file
 *
 * This method is called instead of the getattr() method if the
 * file information is available.
 *
 * Currently this is only called after the create() method if that
 * is implemented (see above).  Later it may be called for
 * invocations of fstat() too.
 *
 * Introduced in version 2.5
 */
// Since it's currently only called after ifs_create(), and ifs_create()
// opens the file, I ought to be able to just use the fd and ignore
// the path...
int ifs_fgetattr(const char *path, struct stat *statbuf, struct fuse_file_info *fi)
{
	AutoTimer _timer(__FUNCTION__);

	int retstat = 0;

	log_msg(LOG_LEVEL_DEBUG, "\nifs_fgetattr(path=\"%s\", statbuf=0x%08x, fi=0x%08x)\n",
		path, statbuf, fi);
	log_fi(fi);

	retstat = fstat(fi->fh, statbuf);
	if (retstat < 0) {
		// This function is used to check file existance
		// Key performance function, get rid of unneccessary performance
		retstat = ifs_error("ifs_fgetattr fstat", 0); // no log
		//log_stat(statbuf);
	}

	return retstat;
}

#include "ifsctl.h"

enum {
        FIOC_NONE,
        FIOC_ROOT,
        FIOC_FILE,
};

static int fioc_file_type(const char *path)
{
	if (strcmp(path, "/") == 0)
	{
		return FIOC_ROOT;
	}
	return FIOC_FILE;
}

int ifs_ioctl(const char *path, int cmd, void *arg, struct fuse_file_info *fi, unsigned int flags, void *data)
{
	(void) arg;
	(void) fi;
	(void) flags;
	log_msg(LOG_LEVEL_ERROR, "\nifs_ioctl received\n");

	if (fioc_file_type(path) != FIOC_FILE) {
		return -EINVAL;
	}
	if (flags & FUSE_IOCTL_COMPAT) {
		return -ENOSYS;
	}

	switch (cmd) {
	case IFSIOC_PRINTDB:
		log_msg(LOG_LEVEL_ERROR, "\nifs_ioctl: PRINTDB\n");

		if(objmap_dump_to_log(1) == -1)
		{
			log_msg(LOG_LEVEL_ERROR, "\nifs_ioctl: fail to print db level 1\n");
			return ifs_error("ifs_ioctl fail to print db level 1", 0);
		}

		if(objmap_dump_to_log(2) == -1)
		{
			log_msg(LOG_LEVEL_ERROR, "\nifs_ioctl: fail to print db level 2\n");
			return ifs_error("ifs_ioctl fail to print db level 2", 0);
		}

		if(postprocess_dump_to_log() == -1)
		{
			log_msg(LOG_LEVEL_ERROR, "\nifs_ioctl: fail to print postprocess db\n");
			return ifs_error("ifs_ioctl fail to print postprocess db", 0);
		}

		if(stats_dump_to_log() == -1)
		{
			log_msg(LOG_LEVEL_ERROR, "\nifs_ioctl: fail to print stats db\n");
			return ifs_error("ifs_ioctl fail to print stats db", 0);
		}

		log_msg(LOG_LEVEL_ERROR, "\nifs_ioctl: PRINTDB done\n");
		return 0;

	case IFSIOC_EVICT:
		log_msg(LOG_LEVEL_ERROR, "\nifs_ioctl: EVICT\n");
		if(process_L1obj_db() == -1)
		{
			log_msg(LOG_LEVEL_ERROR, "\nifs_ioctl: fail to evict\n");
			return ifs_error("ifs_ioctl fail to evict", 0);
		}
		log_msg(LOG_LEVEL_ERROR, "\nifs_ioctl: EVICT done\n");
		return 0;

	}

	return -EINVAL;
}

#ifdef __cplusplus
struct ifs_fuse_operations:fuse_operations {
	ifs_fuse_operations() {
		getattr = ifs_getattr;
		readlink = ifs_readlink;
		// no .getdir -- that's deprecated
		getdir = NULL;
		mknod = ifs_mknod;
		mkdir = ifs_mkdir;
		unlink = ifs_unlink;
		rmdir = ifs_rmdir;
		symlink = ifs_symlink;
		rename = ifs_rename;
		link = ifs_link;
		chmod = ifs_chmod;
		chown = ifs_chown;
		truncate = ifs_truncate;
		utimens = ifs_utimens;
		open = ifs_open;
		read = ifs_read;
		write = ifs_write;
		ioctl = ifs_ioctl;
		statfs = ifs_statfs;
		flush = ifs_flush;
		release = ifs_release;
		fsync = ifs_fsync;
		setxattr = ifs_setxattr;
		getxattr = ifs_getxattr;
		listxattr = ifs_listxattr;
		removexattr = ifs_removexattr;
		opendir = ifs_opendir;
		readdir = ifs_readdir;
		releasedir = ifs_releasedir;
		fsyncdir = ifs_fsyncdir;
		init = ifs_init;
		destroy = ifs_destroy;
		access = ifs_access;
		create = ifs_create;
		ftruncate = ifs_ftruncate;
		fgetattr = ifs_fgetattr;
  }
} ifs_oper;
#else
struct fuse_operations ifs_oper = {
  .getattr = ifs_getattr,
  .readlink = ifs_readlink,
  // no .getdir -- that's deprecated
  .getdir = NULL,
  .mknod = ifs_mknod,
  .mkdir = ifs_mkdir,
  .unlink = ifs_unlink,
  .rmdir = ifs_rmdir,
  .symlink = ifs_symlink,
  .rename = ifs_rename,
  .link = ifs_link,
  .chmod = ifs_chmod,
  .chown = ifs_chown,
  .truncate = ifs_truncate,
  .utimens = ifs_utimens,
  .open = ifs_open,
  .read = ifs_read,
  .write = ifs_write,
  .ioctl = ifs_ioctl,
  .statfs = ifs_statfs,
  .flush = ifs_flush,
  .release = ifs_release,
  .fsync = ifs_fsync,
  .setxattr = ifs_setxattr,
  .getxattr = ifs_getxattr,
  .listxattr = ifs_listxattr,
  .removexattr = ifs_removexattr,
  .opendir = ifs_opendir,
  .readdir = ifs_readdir,
  .releasedir = ifs_releasedir,
  .fsyncdir = ifs_fsyncdir,
  .init = ifs_init,
  .destroy = ifs_destroy,
  .access = ifs_access,
  .create = ifs_create,
  .ftruncate = ifs_ftruncate,
  .fgetattr = ifs_fgetattr
};
#endif

void ifs_usage()
{
    fprintf(stderr, "usage:  routefs [FUSE and mount options] rootDir mountPoint\n");
}

int rfs_init(const char * rootdir)
{
	AutoTimer _timer(__FUNCTION__);

	int mkdir_status = mkdir(rootdir, S_IRWXU);
	log_msg(LOG_LEVEL_ERROR, "rfs_init creating %s\n", rootdir);
	if (mkdir_status != 0 && errno != EEXIST) {
		char errstr[1024];
		log_msg(LOG_LEVEL_ERROR, "rfs_init failed to create %s, returned %d(%s)\n", rootdir, errno, strerror_r(errno, errstr, 1024));
		return mkdir_status;
	}

	default_datadir = std::string(rootdir) + ("/data");
	mkdir_status = mkdir(default_datadir.c_str(), S_IRWXU);
	log_msg(LOG_LEVEL_ERROR, "rfs_init creating %s\n", default_datadir.c_str());
	if (mkdir_status != 0 && errno != EEXIST) {
		char errstr[1024];
		log_msg(LOG_LEVEL_ERROR, "rfs_init failed to create %s, returned %d(%s)\n", default_datadir.c_str(), errno, strerror_r(errno, errstr, 1024));
		return mkdir_status;
	}

	return 0;
}

int main(int argc, char *argv[])
{
	int fuse_stat;
	struct ifs_state *ifs_data;

	// routefs doesn't do any access checking on its own (the comment
	// blocks in fuse.h mention some of the functions that need
	// accesses checked -- but note there are other functions, like
	// chown(), that also need checking!).  Since running routefs as root
	// will therefore open Metrodome-sized holes in the system
	// security, we'll check if root is trying to mount the filesystem
	// and refuse if it is.  The somewhat smaller hole of an ordinary
	// user doing it with the allow_other flag is still there because
	// I don't want to parse the options string.
	//if ((getuid() == 0) || (geteuid() == 0)) {
	//fprintf(stderr, "Running BBFS as root opens unnacceptable security holes\n");
	//return 1;
	//}

	// Perform some sanity checking on the command line:  make sure
	// there are enough arguments, and that neither of the last two
	// start with a hyphen (this will break if you actually have a
	// rootpoint or mountpoint whose name starts with a hyphen, but so
	// will a zillion other programs)
	if ((argc < 3) || (argv[argc-2][0] == '-') || (argv[argc-1][0] == '-')) {
		ifs_usage();
		return 1;
	}

	ifs_data = (struct ifs_state *)malloc(sizeof(struct ifs_state));
	if (ifs_data == NULL) {
		perror("main calloc");
		abort();
	}

	// Pull the rootdir out of the argument list and save it in my
	// internal data
	snprintf(ifs_data->rootdir,  PATH_MAX, "%s", argv[argc-2]);
	//argv[argc-1] = argv[argc-2];
	argv[argc-2] = "-obig_writes";
	// argc--;

	ifs_data->logfile = log_open(LOG_FILENAME);

	// Initialize the type map
	printf("Using rootdir %s\n", ifs_data->rootdir);
	if (0!=rfs_init(ifs_data->rootdir)) {
		printf("Failed to initialize routefs components, check routefs.log for details.\n");
		log_msg(LOG_LEVEL_ERROR, "Failed to initialize routefs components\n");
		return 1;
	}
	// snprintf(ifs_data->rootdir,  PATH_MAX, "%s", default_datadir.c_str());

	// turn over control to fuse
	fprintf(stderr, "about to call fuse_main\n");
	for (int cnt = 0; cnt < argc; cnt++) {
		printf("fuse_main args: %s\n", argv[cnt]);
	}
	fuse_stat = fuse_main(argc, argv, &ifs_oper, ifs_data);
	fprintf(stderr, "fuse_main returned %d\n", fuse_stat);

	return fuse_stat;
}
