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

unsigned til_get_ncpus(void)
{
#ifdef __WIN32__
	SYSTEM_INFO sysinfo;

	GetSystemInfo(&sysinfo);

	return MIN(sysinfo.dwNumberOfProcessors, TIL_MAXCPUS);
#else
 #ifdef __MACH__
	int	count;
	size_t	count_len = sizeof(count);

	if (sysctlbyname("hw.logicalcpu_max", &count, &count_len, NULL, 0) < 0)
		return 1;

	return MIN(count, TIL_MAXCPUS);
 #else
	long	n;

	n = sysconf(_SC_NPROCESSORS_ONLN);
	if (n <= 0)
		return 1;

	return MIN(n, TIL_MAXCPUS);
 #endif
#endif
}
