/*
 * image.h - interfaces for loading/saving/copying OpenCL images.
 */

#ifndef	_IMAGE_H
#define	_IMAGE_H

#include "types.h"

/*
 * Check whether there is an image available from some other source.
 * If there is, load it into the passed-in OpenCL image, and return true.
 */
extern bool
image_available(cl_mem);

/*
 * Save the current OpenCL image. "steps" refers to the number of steps
 * that have been executed thus far; it is just used to name the file.
 */
extern void
image_save(cl_mem, int steps);

/*
 * Called to preserve the current image before a resize.
 */
extern void
image_preserve(pix_t, pix_t, cl_mem);

/*
 * Copy an "ow"x"oh"-sized image, located at "oi", into a "nw"x"nh"-sized
 * image, located at "ni".
 */
extern void
image_copy(const pix_t ow, const pix_t oh, const uint8_t *oi,
    const pix_t nw, const pix_t nh, uint8_t *ni,
    void (*copy_cb)(const uint8_t *oi, uint8_t *ni, pix_t op, pix_t np));

/* ------------------------------------------------------------------ */

extern void
image_datafile(char *file);

#endif	/* _IMAGE_H */
