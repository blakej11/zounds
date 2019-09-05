/*
 * boxparams.h - interfaces for setting/getting box blur parameters.
 */

#ifndef	_BOXPARAMS_H
#define	_BOXPARAMS_H

#include "types.h"

/*
 * Given a radius, decide which box blur kernel to use, and how many
 * subblocks to use if using the subblock kernel.
 */
extern box_kernel_t
boxparams_get(pix_t radius, blkidx_t *nblkp);

/* ------------------------------------------------------------------ */

extern void
boxparams_init(void);

extern void
boxparams_init_manual(blkidx_t nblk, box_kernel_t bk);

#endif	/* _BOXPARAMS_H */

