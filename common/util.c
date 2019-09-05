/*
 * util.c - utility routines used by all parts of the code.
 * These don't depend on any other part of the program.
 */
#include <stdlib.h>

#include "debug.h"
#include "util.h"

/* ------------------------------------------------------------------ */

void *
mem_alloc(size_t bytes)
{
	void *p = malloc(bytes);

	if (p == NULL) {
		die("failed to allocate %d bytes, exiting\n");
	}

	return (p);
}

void
mem_free(void **p)
{
	free(*p);
	*p = NULL;
}
