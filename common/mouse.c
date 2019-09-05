/*
 * mouse.c - handle mouse movement data.
 */
#include "common.h"

#include "debug.h"
#include "module.h"
#include "stroke.h"
#include "window.h"

static void mouse_cb(int x, int y, bool down);
static void motion_cb(int x, int y);

/* ------------------------------------------------------------------ */

static struct {
	int	X;			/* Last known mouse position. */
	int	Y;
} Mouse;

static void
mouse_preinit(void)
{
	window_set_mouse_cb(mouse_cb);
	window_set_motion_cb(motion_cb);

	debug_register_toggle('m', "mouse", DB_MOUSE, NULL);
}

const module_ops_t	mouse_ops = {
	mouse_preinit,
	NULL,
	NULL
};

/* ------------------------------------------------------------------ */

/*
 * Called when a mouse button is pressed or released.
 * We only care about "left button" action.
 */
static void
mouse_cb(int x, int y, bool down)
{
	if (x < 0 || y < 0 || x > Width || y > Height) {
		debug(DB_MOUSE, "mouse_cb: ignoring [%d, %d]\n", x, y);
		return;
	}

	if (down) {
		Mouse.X = x;
		Mouse.Y = y;
	} else {
		/*
		 * Shouldn't happen that we get an UP without a DOWN,
		 * but this stuff is notoriously finicky.
		 */
		if (Mouse.X != -1 && Mouse.Y != -1) {
			stroke_add(Mouse.X, Mouse.Y, x, y);
			window_update();
		}
		Mouse.X = -1;
		Mouse.Y = -1;
	}
}

/*
 * Called when the mouse is dragged.  We take the old and new coordinates
 * and pass them to the stroke subsystem.
 */
static void
motion_cb(int x, int y)
{
	debug(DB_MOUSE, "motion_cb: got [%d, %d]\n", x, y);
	if (x < 0 || y < 0 || x > Width || y > Height) {
		debug(DB_MOUSE, "motion_cb: ignoring [%d, %d]\n", x, y);
		return;
	}
	if (Mouse.X != -1 && Mouse.Y != -1) {
		stroke_add(Mouse.X, Mouse.Y, x, y);
		window_update();
	}
	Mouse.X = x;
	Mouse.Y = y;
}
