/*
 * window.h - code for controlling the display of an image to the screen.
 * This code interacts with GLUT, so no one else has to.
 */

#ifndef	_WINDOW_H
#define	_WINDOW_H

/*
 * Returns the current magnification being used for generating images.
 * 1.0 = the image is displayed as it is generated;
 * 2.0 = the image generated is twice as large as the image displayed.
 */
extern float
window_getscale(void);

/*
 * Indicate that the display should be refreshed.
 */
extern void
window_update(void);

/*
 * Returns true if and only if we're displaying graphics to the screen.
 */
extern bool
window_graphics(void);

/*
 * Registers a callback to handle all keypresses.
 *
 * The callback's argument is the ASCII value of the key pressed.
 */
extern void
window_set_keyboard_cb(void (*cb)(unsigned char));

/*
 * Registers a callback to handle all mouse button events.
 *
 * The callback's arguments are the x and y coordinates of the mouse when
 * the button was pressed, and true/false indicating button press/release.
 */
extern void
window_set_mouse_cb(void (*cb)(int, int, bool));

/*
 * Registers a callback to handle all mouse movement events.
 *
 * The callback's arguments are the new x and y coordinates of the mouse.
 */
extern void
window_set_motion_cb(void (*cb)(int, int));

/* ------------------------------------------------------------------ */

extern void
window_create(pix_t, pix_t);

extern void
window_setscale(float);

extern void
window_fullscreen(void);

extern void
window_set_animated(bool);

extern void
window_set_graphics(bool);

extern void
window_saveperiod(time_t);

extern void
window_mainloop(void);

#endif	/* _WINDOW_H */
