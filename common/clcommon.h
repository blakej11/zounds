/*
 * clcommon.h - types and macros used by both C and OpenCL.
 */

#ifndef	_CLCOMMON_H
#define	_CLCOMMON_H

/*
 * A type representing a count of pixels in one dimension (e.g. width or
 * height) or two dimensions (e.g. a pixel index into an image).
 */
typedef	unsigned int		pix_t;

/*
 * A type representing a pixel count that can be negative, e.g. the difference
 * between two pix_t's.
 */
typedef	int			spix_t;

/*
 * A type representing a block index, for the subblock box code.
 *
 * Although actual block indices are non-negative integers, the implementation
 * uses values outside the actual range to represent wrapping around the
 * bottom or the top of the block table.  So this is a signed type.
 */
typedef int			blkidx_t;

#endif	/* _CLCOMMON_H */
