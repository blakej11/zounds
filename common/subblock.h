/*
 * subblock.h - common code for subblock box blur processing.
 *
 * This header file is #include'd by both C and OpenCL code.
 */

#ifndef	_SUBBLOCK_H
#define	_SUBBLOCK_H

/*
 * The subblock code maintains a table of parameters used by the subblock
 * kernel.  It uses these #define's to set the table's size at compile time,
 * to allow faster access by the kernel.  These values are chosen based on
 * current behavior by the rest of the code, and they can be freely changed.
 *
 * MAX_RADIUS is the largest radius that will be performed by the box blur.
 * MAX_NBLOCKS is the largest value of "nblocks" that will be seen.
 */
#define	MAX_RADIUS	512
#define	MAX_NBLOCKS	1024

/*
 * The parameters used for a given block's subblock operation.
 * There is one of these per possible block and radius value.
 * The number of blocks per radius is assumed to be a fixed mapping.
 */
typedef struct {
	short	sp_lblk;	/* number of leftward blocks needed */
	short	sp_lpix;	/* number of leftward pixels needed */
	short	sp_rblk;	/* number of rightward blocks needed */
	short	sp_rpix;	/* number of rightward pixels needed */
} subblock_params_t;

#endif	/* _SUBBLOCK_H */

