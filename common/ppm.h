/*
 * ppm.h - interfaces for reading and writing PPM image files.
 *
 * Note that this code can only read raw PPM files (with header "P6"),
 * and not all-text PPM files (with header "P3").
 */

#ifndef	_PPM_H
#define	_PPM_H

#include "types.h"
#include "common.h"

/*
 * Read the width and height from the PPM header of the specified file.
 * Returns true if the file is a valid PPM file, and false otherwise.
 */
extern bool
ppm_read_sizes(const char *filename, pix_t *widthp, pix_t *heightp);

/*
 * Read the specified PPM file into "rgb". "rgb" is a buffer with size
 * width * height * 3 (one byte for each of R, G, and B).
 */
extern bool
ppm_read_rgb(const char *filename,
    pix_t width, pix_t height, uint8_t *rgb);

/*
 * Create the specified PPM file from "rgb". "rgb" is a buffer with size
 * width * height * 3 (one byte for each of R, G, and B).
 */
extern void
ppm_write_rgb(const char *filename,
    const uint8_t *rgb, pix_t width, pix_t height);

#endif	/* _PPM_H */
