#ifndef _RKT_SCENER_H
#define _RKT_SCENER_H

/* these are made public for rkt's setup func */
#define RKT_SCENER_DEFAULT_PORT		54321
#define RKT_SCENER_DEFAULT_ADDRESS	"127.0.0.1"

typedef struct rkt_context_t rkt_context_t;

int rkt_scener_startup(rkt_context_t *ctxt);
int rkt_scener_update(rkt_context_t *ctxt);
int rkt_scener_shutdown(rkt_context_t *ctxt);

#endif
