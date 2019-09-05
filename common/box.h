/*
 * box.h - interfaces for performing box blur.
 */

#ifndef	_BOX_H
#define	_BOX_H

#include "types.h"

/*
 * Perform a 2-D box blur of the buffer at "src", using the specified radius
 * (i.e. box width/height), and store the result into "dst".
 */
extern void
box_blur(cl_mem src, cl_mem dst, pix_t radius, int nbox);

/* ------------------------------------------------------------------ */

extern void
box_test(pix_t min_radius, pix_t max_radius);

#endif	/* _BOX_H */

