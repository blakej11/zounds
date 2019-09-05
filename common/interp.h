/*
 * interp.h - interfaces for controlling the image interpolotor.
 */

#ifndef	_INTERP_H
#define	_INTERP_H

#include "types.h"

/*
 * Enable the image interpolator. "dflt" is the default number of interpolated
 * images, and "max" is the maximum number of interpolated images.
 */
extern void
interp_enable(int dflt, int max);

/*
 * A hook to be called by the core algorithm's import() routine.
 * The load() callback loads the next non-interpolated image.
 */
extern void
interp_load(cl_mem data, void (*load)(cl_mem));

/*
 * A hook to be called by the core algorithm's step_and_export() routine.
 * The step() callback generates the next non-interpolated image.
 */
extern void
interp_step(cl_mem result, float min, float max, void (*step)(cl_mem));

#endif	/* _INTERP_H */
