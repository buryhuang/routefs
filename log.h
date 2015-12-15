/*
  Copyright (C) 2012 Joseph J. Pfeiffer, Jr., Ph.D. <pfeiffer@cs.nmsu.edu>

  This program can be distributed under the terms of the GNU GPLv3.
  See the file COPYING.
*/

#ifndef _LOG_H_
#define _LOG_H_
#include <stdio.h>

enum LOG_LEVEL {
	LOG_LEVEL_DEBUG,
	LOG_LEVEL_WARN,
	LOG_LEVEL_ERROR
};

#define DEF_LOG_LEVEL LOG_LEVEL_ERROR

//  macro to log fields in structs.
#define log_struct(st, field, format, typecast) \
  log_msg(LOG_LEVEL_DEBUG, "    " #field " = " #format "\n", typecast st->field)

#ifdef __cplusplus
extern "C" {
#endif

FILE *log_open(const char * logname);
void log_fi (struct fuse_file_info *fi);
void log_stat(struct stat *si);
void log_statvfs(struct statvfs *sv);
void log_utime(struct utimbuf *buf);

void log_msg(LOG_LEVEL level, const char *format, ...);

extern FILE* _log_file;

#ifdef __cplusplus
}
#endif

#endif
