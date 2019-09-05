/*
 * util.h - utility routines.
 */

#ifndef	_UTIL_H
#define	_UTIL_H

#include "types.h"

/*
 * Allocates "bytes" worth of memory and returns it.
 * Exits the program if the allocation fails.
 */
extern void *
mem_alloc(size_t bytes);

/*
 * Frees the memory pointed to by *p (not "p"!), and sets *p to NULL.
 */
extern void
mem_free(void **p);

#endif	/* _UTIL_H */
