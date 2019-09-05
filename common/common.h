/*
 * common.h - macros and global data used by all of the C code.
 */

#ifndef	_COMMON_H
#define	_COMMON_H

#include "types.h"
#include "clcommon.h"

/*
 * The two pieces of global data.
 */
extern pix_t	Width;		/* width of image */
extern pix_t	Height;		/* height of image */

/*
 * Useful macros.
 */
#define	MIN(a, b)		(((a) < (b)) ? (a) : (b))
#define	MAX(a, b)		(((a) > (b)) ? (a) : (b))
#define	P2ALIGN(x, align)	((x) & -(align))
#define	P2ROUNDUP(x, align)	(-(-(x) & -(align)))

#endif	/* _COMMON_H */
