#include <limits.h>
#include <stdio.h>
#include <string.h>
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


static void query(const char *prompt, const char *def, char *buf, int len)
{
	buf[0] = '\0';

	printf("%s [%s]: ", prompt, def);
	fflush(stdout);

	fgets(buf, len, stdin);
	if (buf[0] == '\0' || buf[0] == '\n') {
		snprintf(buf, len, "%s", def);
	} else if(strchr(buf, '\n')) {
		*strchr(buf, '\n') = '\0';
	}
}


void ask_string(char *buf, int len, const char *prompt, const char *def)
{
	query(prompt, def, buf, len);
}


void ask_num(int *res, int max, const char *prompt, int def)
{
	char	buf[21], buf2[256];
	int	num;

	snprintf(buf, sizeof(buf), "%i", def);
	do {
		query(prompt, buf, buf2, sizeof(buf2));
		num = atoi(buf2); /* TODO: errors (strtol)*/
	} while (num > max);

	*res = num;
}
