/*
 * datasrc.h - interfaces for generating a new image.
 */

#ifndef	_DATASRC_H
#define	_DATASRC_H

#include "types.h"

/*
 * Generate the next image.  "image" is an image2d_t of RGBA float's.
 */
extern void
datasrc_step(cl_mem);

/*
 * Re-render the current image. Called prior to a window resize operation.
 */
extern void
datasrc_rerender(cl_mem);

/*
 * Register a callback to be called (with argument "arg") nsteps steps from
 * now.
 */
extern void
datasrc_step_registercb(int nsteps, void (*cb)(void *), void *arg);

#endif	/* _DATASRC_H */
