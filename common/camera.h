/*
 * camera.h - routines for interfacing with a video camera.
 */

#ifndef	_CAMERA_H
#define	_CAMERA_H

#include "types.h"

/*
 * Load an image from a camera.
 */
extern bool		load_camera_cb(pix_t, pix_t, uint8_t *);

#endif	/* _CAMERA_H */
