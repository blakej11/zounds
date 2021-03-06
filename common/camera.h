/*
 * camera.h - routines for interfacing with a video camera.
 */

#ifndef	_CAMERA_H
#define	_CAMERA_H

#include "types.h"

extern void	camera_disable(void);
extern bool	camera_disabled(void);
extern bool	camera_try_file(const char *, pix_t *, pix_t *);
extern void	camera_set_filename(const char *);
extern bool	camera_init(void);
extern bool	camera_initialized(void);
extern pix_t	camera_width(void);
extern pix_t	camera_height(void);
extern bool	camera_grab(void);
extern uint8_t *camera_retrieve(void);
extern void	camera_fini(void);

#endif	/* _CAMERA_H */
