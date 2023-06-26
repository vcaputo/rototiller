#ifndef _TIL_STR_H
#define _TIL_STR_H

typedef struct til_str_t til_str_t;

til_str_t * til_str_new(const char *seed);
void * til_str_free(til_str_t *str);
til_str_t * til_str_newf(const char *format, ...);
int til_str_appendf(til_str_t *str, const char *format, ...);
char * til_str_strdup(const til_str_t *str);
const char * til_str_buf(const til_str_t *str, size_t *res_len);
char * til_str_to_buf(til_str_t *str, size_t *res_len);
til_str_t * til_str_chomp(til_str_t *str);

#endif
