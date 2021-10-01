#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#ifdef __WIN32__
#include <windows.h>
#endif

#include "til_util.h"

#define TIL_SYSFS_CPU	"/sys/devices/system/cpu/cpu"
#define TIL_MAXCPUS		1024

unsigned til_get_ncpus(void)
{
#ifdef __WIN32__
	SYSTEM_INFO sysinfo;

	GetSystemInfo(&sysinfo);

	return sysinfo.dwNumberOfProcessors;
#else
	char		path[cstrlen(TIL_SYSFS_CPU "1024") + 1];
	unsigned	n;

	for (n = 0; n < TIL_MAXCPUS; n++) {
		snprintf(path, sizeof(path), "%s%u", TIL_SYSFS_CPU, n);
		if (access(path, F_OK) == -1)
			break;
	}

	return n == 0 ? 1 : n;
#endif
}
