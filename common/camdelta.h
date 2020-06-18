/*
 * camdelta.h - routines for taking the difference of two camera frames.
 */

#ifndef	_CAMDELTA_H
#define	_CAMDELTA_H

#include "types.h"

extern float	camdelta_intensity(void);
extern void	camdelta_step(cl_mem newdelta);

#endif	/* _CAMDELTA_H */
