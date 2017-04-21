#ifndef _ROTOTILLER_H
#define _ROTOTILLER_H

typedef struct rototiller_module_t {
	void	(*render_fragment)(fb_fragment_t *);
	char	*name;
	char	*description;
	char	*author;
	char	*license;
} rototiller_module_t;

#endif
