/*
 * stroke.h - handles mouse strokes.
 */

#ifndef	_STROKE_H
#define	_STROKE_H

#include "types.h"

/*
 * Returns true if there is at least one mouse stroke pending.
 */
extern bool
stroke_pending(void);

/*
 * Add a mouse stroke to the queue, corresponding to a movement from
 * <ox,oy> to <nx,ny>.
 */
extern void
stroke_add(pix_t ox, pix_t oy, pix_t nx, pix_t ny);

/* ------------------------------------------------------------------ */

extern void
stroke_step(cl_mem, cl_mem);

#endif	/* _STROKE_H */
