#include <sys/types.h>
#include <sys/uio.h>
#include <sys/ioctl.h>

enum
{
	IFSIOC_PRINTDB = _IOW('E', 0, size_t),
	IFSIOC_EVICT   = _IOW('E', 1, size_t),
};

struct ifs_ioctl_arg
{
    off_t           offset;
    void            *buf;
};
