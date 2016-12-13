#ifndef _ROTOTILLER_H
#define _ROTOTILLER_H

typedef struct rototiller_renderer_t {
	void	(*render)(fb_fragment_t *);
	char	*name;
	char	*description;
	char	*author;
	char	*license;
} rototiller_renderer_t;

#endif
