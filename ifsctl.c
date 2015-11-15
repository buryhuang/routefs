/*
  FUSE fioclient: FUSE ioctl example client
  Copyright (C) 2008       SUSE Linux Products GmbH
  Copyright (C) 2008       Tejun Heo <teheo@suse.de>
  This program can be distributed under the terms of the GNU GPL.
  See the file COPYING.
*/
#include <sys/types.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include "ifsctl.h"
const char *usage =
	"Usage: ifsctl FIOC_FILE COMMAND\n"
	"\n"
	"COMMANDS\n"
	"  e evict L1 cache\n"
	"\n";

int main(int argc, char **argv)
{
	char cmd;
	int fd;
	if (argc < 2)
	    goto usage;
	fd = open(argv[1], O_RDWR | O_CREAT, S_IRWXU);
	if (fd < 0)
	{
		perror("open");
		return 1;
	}
	cmd = tolower(argv[2][0]);
	
	struct ifs_ioctl_arg arg;
	switch (cmd)
	{
	case 'p':
		if (ioctl(fd, IFSIOC_PRINTDB, &arg))
		{
			perror("ioctl IFSIOC_PRINTDB");
			return 1;
		}
		printf("IFSIOC_PRINTDB done.\n");
		return 0;
	
	case 'e':
		if (ioctl(fd, IFSIOC_EVICT, &arg))
		{
			perror("ioctl IFSIOC_EVICT");
			return 1;
		}
		printf("IFSIOC_EVICT done.\n");
		return 0;
		}

usage:
	fprintf(stderr, "%s", usage);
	return 1;
}

