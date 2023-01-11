#include <signal.h>
#include <stdio.h>
#include <sys/time.h>

#include "til_fb.h"
#include "til_util.h"


static int	print_fps;


static void sigalrm_handler(int signum)
{
	print_fps = 1;
}


int fps_setup(void)
{
#ifdef __WIN32__

#else
	struct itimerval	interval = { 	
					.it_interval = { .tv_sec = 1, .tv_usec = 0 },
					.it_value = { .tv_sec = 1, .tv_usec = 0 },
				};

	if (signal(SIGALRM, sigalrm_handler) == SIG_ERR)
		return 0;
		
	if (setitimer(ITIMER_REAL, &interval, NULL) < 0)
		return 0;

	return 1;
#endif
}


void fps_fprint(til_fb_t *fb, FILE *out)
{
#ifdef __WIN32__

#else
	unsigned	n;

	if (!print_fps)
		return;

	til_fb_get_put_pages_count(fb, &n);
	fprintf(out, "FPS: %u\n", n);

	print_fps = 0;
#endif
}
