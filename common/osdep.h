/*
 * osdep.h - routines with differing implementations on different OSes.
 */

#ifndef	_OSDEP_H
#define	_OSDEP_H

#include "types.h"

/*
 * Get a timestamp in nanoseconds.
 * (May not have nanosecond accuracy, but does have nanosecond precision.)
 */
#ifndef __sun
typedef long long hrtime_t;
extern hrtime_t		gethrtime(void);
#endif

/*
 * Create an OpenCL context.
 */
extern cl_context	create_cl_context(void);

/*
 * Create a GLUT context.
 */
extern void		create_glut_context(void);

#endif	/* _OSDEP_H */
