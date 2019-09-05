/*
 * texture.h - interfaces for using OpenGL to render an image as a texture.
 */

#ifndef	_TEXTURE_H
#define	_TEXTURE_H

#include "types.h"

/*
 * Creates an OpenGL texture. "width_fraction" and "height_fraction" refer to
 * the ratio of the image's width and height to the screen's width and height.
 */
extern int
texture_init(float width_fraction, float height_fraction);

/*
 * Render the contents of the texture to the screen.
 */
extern void
texture_render(void);

/*
 * Destroys the OpenGL texture.
 */
extern void
texture_fini(void);

#endif	/* _TEXTURE_H */
