#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#ifdef __WIN32__
#include <windows.h>
#endif

#ifdef __MACH__
#include <sys/types.h>
#include <sys/sysctl.h>
#endif

#include "til_limits.h"
#include "til_util.h"

#define TIL_SYSFS_CPU	"/sys/devices/system/cpu/cpu"

unsigned til_get_ncpus(void)
{
#ifdef __WIN32__
	SYSTEM_INFO sysinfo;

	GetSystemInfo(&sysinfo);

	return MIN(sysinfo.dwNumberOfProcessors, TIL_MAXCPUS);
#endif

#ifdef __MACH__
	int count;
	size_t count_len = sizeof(count);

	if (sysctlbyname("hw.logicalcpu_max", &count, &count_len, NULL, 0) < 0)
		return 1;

	return MIN(count, TIL_MAXCPUS);
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
