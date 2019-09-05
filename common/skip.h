/*
 * skip.h - manages image skipping.
 */

#ifndef	_SKIP_H
#define	_SKIP_H

#include "types.h"

/*
 * The image skipping engine. Should be called from a core algorithm's
 * step_and_export() routine.
 *
 * "result" is the image2d_t that is passed to step_and_export().
 * "step" is the core algorithm's real callback for generating images.
 * "min" and "max" are the minimum and maximum values for all components
 * of a datavec.
 * "dim" indicates which dimension of the datavec the image skipping engine
 * should use.
 */
extern void
skip_step(cl_mem result, int dim, float min, float max, void (*step)(cl_mem));

#endif	/* _SKIP_H */
