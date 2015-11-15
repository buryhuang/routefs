#ifndef __SSFS_H__
#define __SSFS_H__

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

#ifdef __cplusplus
extern "C" {
#endif
int ifs_getattr(const char *path, struct stat *statbuf);
int ifs_readlink(const char *path, char *link, size_t size);
int ifs_mknod(const char *path, mode_t mode, dev_t dev);
int ifs_mkdir(const char *path, mode_t mode);
int ifs_release(const char *path, struct fuse_file_info *fi);
int ifs_create(const char *path, mode_t mode, struct fuse_file_info *fi);

#ifdef __cplusplus
}
#endif

#endif
