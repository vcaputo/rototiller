#include <limits.h>
#include <stdio.h>
#include <unistd.h>

#include "util.h"

#define SYSFS_CPU	"/sys/devices/system/cpu/cpu"
#define MAXCPUS		1024

unsigned get_ncpus(void)
{
	char		path[cstrlen(SYSFS_CPU "1024") + 1];
	unsigned	n;

	for (n = 0; n < MAXCPUS; n++) {
		snprintf(path, sizeof(path), "%s%u", SYSFS_CPU, n);
		if (access(path, F_OK) == -1)
			break;
	}

	return n == 0 ? 1 : n;
}
