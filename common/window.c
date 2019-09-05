/*
 * window.c - code for controlling the display of an image to the screen.
 * All of the GLUT interaction happens here and in osdep.c.
 */

#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "common.h"
#include "gfxhdr.h"

#include "datasrc.h"
#include "debug.h"
#include "image.h"
#include "keyboard.h"
#include "module.h"
#include "opencl.h"
#include "osdep.h"
#include "texture.h"
#include "window.h"

/* ------------------------------------------------------------------ */

/*
 * The one bit of globally visible data.  These are referenced in so many
 * places that it's not worthwhile to try to "hide" them.
 *
 * See module.h for the ramifications of relying on Width and Height.
 */
pix_t		Width, Height;		/* The thing we're computing. */

static struct {
	pix_t	view_width;		/* Size of our view onto the image */
	pix_t	view_height;
	pix_t	old_width;		/* View size before going fullscreen */
	pix_t	old_height;
	bool	fullscreen;

	int	steps;			/* Number of images we've displayed */
	bool	nogfx;			/* Whether we're running headless */
	bool	update;			/* Whether to force an update */
	bool	animated;
	bool	save_ongoing;		/* Whether to save every image */
	time_t	save_period;		/* How often to save images */
	time_t	last_period_save;	/* If save_period, when last done */
	float	scale;			/* View-to-image scale factor */

	cl_mem	gl_image;		/* The image for display. */

	float	width_fraction;		/* What magnification we're using */
	float	height_fraction;

	void	(*keyboard_cb)(unsigned char);
	void	(*mouse_cb)(int, int, bool);
	void	(*motion_cb)(int, int);
} Win;	/* X11 thinks it owns the symbol "Window", as a type. Sigh. */

/* ------------------------------------------------------------------ */

/*
 * Indicate whether to run with graphics.
 * (Skipping graphics allows the code to be tested without needing a display.)
 */
void
window_set_graphics(bool graphics)
{
	Win.nogfx = !graphics;
}

/*
 * Are we running with graphics?
 */
bool
window_graphics(void)
{
	return (!Win.nogfx);
}

/*
 * Trigger a window update.
 */
void
window_update(void)
{
	Win.update = true;
}

/*
 * Drop out of fullscreen mode.
 */
static void
key_esc(void)
{
	window_update();

	if (window_graphics() && Win.fullscreen) {
		Win.fullscreen = !Win.fullscreen;
		glutReshapeWindow(Win.old_width, Win.old_height);
		glutPositionWindow(100, 100);
	}
}

/*
 * Toggle fullscreen mode.
 */
static void
key_f(void)
{
	window_update();

	if (window_graphics()) {
		Win.fullscreen = !Win.fullscreen;
		if (Win.fullscreen) {
			Win.old_width = Win.view_width;
			Win.old_height = Win.view_height;
			glutFullScreen();
		} else {
			glutReshapeWindow(Win.old_width, Win.old_height);
			glutPositionWindow(100, 100);
		}
	}
}

/*
 * Save the current image to a PPM file.
 */
static void
window_save(void)
{
	image_save(Win.gl_image, Win.steps);
}

/*
 * Save all following images to PPM files.
 */
static void
key_S(void)
{
	Win.save_ongoing = !Win.save_ongoing;
	if (Win.save_ongoing) {
		window_save();
	}
}

/*
 * Toggle animation.
 */
static void
key_space(void)
{
	Win.animated = !Win.animated;
	window_update();
}

/*
 * This is just here to throw away keypresses that generate multiple chars.
 * If I were feeling more fancy, I'd have it de-UTF8 the key sequence or
 * whatever, but nope nope nope.
 */
static void
key_noop(void)
{
}

/*
 * Explicitly enable/disable animation.
 */
void
window_set_animated(bool animated)
{
	Win.animated = animated;
	window_update();
}

static void
window_stamp(const char *msg)
{
	static hrtime_t	then = 0;
	const hrtime_t	now = gethrtime();
	const hrtime_t	delta = (then == 0 ? 0 : now - then) / 1000;

	debug(DB_WINDOW, "%16lld %5lld.%03lld %s\n",
	    now / 1000, delta / 1000, delta % 1000, msg);
	then = now;
}

/*
 * Generate a new image, and display it.
 */
static void
window_step(void)
{
	debug(DB_WINDOW, "\n");		// start of a new round
	window_stamp("window_step start");

	if (!Win.animated && !Win.update) {
		return;
	}

	Win.update = false;
	Win.steps++;

	/*
	 * Get a new image and render it into the displayable image.
	 */
	datasrc_step(Win.gl_image);

	/*
	 * Show the displayable image on the screen.
	 */
	if (window_graphics()) {
		if (debug_enabled(DB_PERF) && Win.steps > 1) {
			hrtime_t x;
			x = gethrtime();

			clgl_cl_release(Win.gl_image);
			texture_render();
			glutSwapBuffers();
			glFinish();
			clgl_cl_acquire(Win.gl_image);

			x = gethrtime() - x;
			debug(DB_PERF, " + %5.2lf\n", (double)x / 1000000.0);
		} else {
			clgl_cl_release(Win.gl_image);
			texture_render();
			glutSwapBuffers();
			clgl_cl_acquire(Win.gl_image);
		}
	} else {
		debug(DB_PERF, "\n");
	}

	if (Win.save_ongoing) {
		window_save();
	}

	if (Win.save_period != 0) {
		const time_t	t = time(NULL);
		const time_t	sp = Win.save_period;

		if (t / sp != Win.last_period_save / sp) {
			const time_t	now = time(NULL);
			Win.last_period_save = t;
			verbose(DB_WINDOW, "Auto-save at %s", ctime(&t));
			window_save();
		}
	}

	window_stamp("window_step end");
}

/* ------------------------------------------------------------------ */

static void
window_preinit(void)
{
	key_register(27,  KB_DEFAULT, "escape from fullscreen",  key_esc);
	key_register('f', KB_DEFAULT, "enter fullscreen",        key_f);

	key_register('s', KB_DEFAULT, "save current image",      window_save);
	key_register('.', KB_KEYPAD,  "save current image",      window_save);
	key_register('S', KB_DEFAULT, "start saving all images", key_S);
	key_register(' ', KB_DEFAULT, "toggle animation",        key_space);
	key_register('\r', KB_DEFAULT, NULL,                     window_update);
	key_register('\n', KB_DEFAULT, "render one image",       window_update);

	/*
	 * The "clear" key on my keypad sends 0xef 0x9c 0xb9.
	 */
	key_register(0xef, KB_KEYPAD, "toggle animation",        key_space);
	key_register(0x9c, KB_KEYPAD, "",                        key_noop);
	key_register(0xb9, KB_KEYPAD, "",                        key_noop);

	/*
	 * When GLUT decides it's time to display another image,
	 * window_step() will be called.
	 */
	if (window_graphics()) {
		glutDisplayFunc(window_step);
	}

	debug_register_toggle('W', "window handling", DB_WINDOW, NULL);
}

static void
window_init(void)
{
	if (window_graphics()) {
		/*
		 * Create an OpenGL image of type GL_RGBA / GL_UNSIGNED_BYTE.
		 */
		const int	texture_id =
		    texture_init(Win.width_fraction, Win.height_fraction);

		Win.gl_image = clgl_makeimage(GL_TEXTURE_2D, texture_id);
		clgl_cl_acquire(Win.gl_image);
	} else {
		/*
		 * Create an image that acts the same as the GL image would.
		 */
		Win.gl_image = ocl_image_create(CL_RGBA, CL_UNORM_INT8,
		    Width, Height);
	}
}

static void
window_fini(void)
{
	if (window_graphics()) {
		clgl_cl_release(Win.gl_image);
		buffer_free(&Win.gl_image);
		texture_fini();
	} else {
		buffer_free(&Win.gl_image);
	}
}

const module_ops_t	window_ops = {
	window_preinit,
	window_init,
	window_fini
};

/* ------------------------------------------------------------------ */

static void
set_size(pix_t vw, pix_t vh)
{
	const pix_t	iw = (pix_t)((float)vw * Win.scale);
	const pix_t	ih = (pix_t)((float)vh * Win.scale);

	verbose(DB_WINDOW, "Image size: [%u,%u] -> [%u,%u]\n",
	    Width, Height, iw, ih);
	debug(DB_WINDOW, "View size:  [%u,%u] -> [%u,%u]\n",
	    Win.view_width, Win.view_height, vw, vh);

	/*
	 * This is the only place where Width and Height are updated.
	 * Any subsystem which has data based on these values must have
	 * an init() and fini() callback registered, so it can clean up
	 * and reinitialize once we know the new size.
	 */
	Width = iw;
	Height = ih;
	Win.view_width = vw;
	Win.view_height = vh;
	Win.width_fraction = MIN((float)iw / (float)vw, 1.0f);
	Win.height_fraction = MIN((float)ih / (float)vh, 1.0f);
}

static void
reshape_cb(int w, int h)
{
	const pix_t	vw = (pix_t)w;
	const pix_t	vh = (pix_t)h;
	const pix_t	iw = (pix_t)((float)vw * Win.scale);
	const pix_t	ih = (pix_t)((float)vh * Win.scale);
	const bool	change_image = (Width != iw || Height != ih);

	debug(DB_WINDOW, "reshape_cb: invoked\n");

	if (change_image) {
		debug(DB_WINDOW, "reshape_cb: preparing to resize\n");

		if (Win.steps != 0) {
			datasrc_rerender(Win.gl_image);
			image_preserve(Width, Height, Win.gl_image);
		}

		module_fini();
	}

	set_size(vw, vh);

	if (change_image) {
		module_init();
		debug(DB_WINDOW, "reshape_cb: done resizing\n");
	}

	if (window_graphics()) {
		glViewport(0, 0, Win.view_width, Win.view_height);
		clgl_cl_release(Win.gl_image);
		glutSwapBuffers();
		clgl_cl_acquire(Win.gl_image);
	}
}

static void
redisplay_cb(void)
{
	if (window_graphics()) {
		glutPostRedisplay();
	}
}

static void
keyboard_cb(unsigned char key, int x, int y)
{
	if (Win.keyboard_cb) {
		(*Win.keyboard_cb)(key);
		redisplay_cb();
	}
}

static void
mouse_cb(int button, int state, int x, int y)
{
	if (Win.mouse_cb && button == GLUT_LEFT_BUTTON) {
		const int	sx = (int)(Win.scale * x);
		const int	sy = (int)(Win.scale * y);
		(*Win.mouse_cb)(sx, sy, state == GLUT_DOWN);
		redisplay_cb();
	}
}

static void
motion_cb(int x, int y)
{
	window_stamp("motion_cb start");
	if (Win.motion_cb) {
		const int	sx = (int)(Win.scale * x);
		const int	sy = (int)(Win.scale * y);
		(*Win.motion_cb)(sx, sy);
		redisplay_cb();
	}
	window_stamp("motion_cb end");
}

void
window_saveperiod(time_t period)
{
	Win.save_period = period;
}

float
window_getscale(void)
{
	return (Win.scale);
}

/*
 * Changing this dynamically would require a trip through init/fini,
 * so right now we don't do that.
 */
void
window_setscale(float scale)
{
	Win.scale = scale;
}

void
window_set_keyboard_cb(void (*cb)(unsigned char))
{
	Win.keyboard_cb = cb;
}

void
window_set_mouse_cb(void (*cb)(int, int, bool))
{
	Win.mouse_cb = cb;
}

void
window_set_motion_cb(void (*cb)(int, int))
{
	Win.motion_cb = cb;
}

void
window_create(pix_t w, pix_t h)
{
	if (Win.scale == 0) {
		Win.scale = 1;
	}

	/*
	 * Need to have Win.view_width and Win.view_height set
	 * before calling glutInitWindowSize().
	 */
	set_size(w, h);

	if (window_graphics()) {
		create_glut_context();

		glutInitWindowSize(Win.view_width, Win.view_height);
		glutInitWindowPosition(0, 0);
		glutCreateWindow("");
	}
}

void
window_fullscreen(void)
{
	Win.old_width = Win.view_width;
	Win.old_height = Win.view_height;
	Win.fullscreen = true;
	if (window_graphics()) {
		/*
		 * It seems like this shouldn't be necessary. But on Linux,
		 * if we don't do this, we get one reshape callback that
		 * doesn't actually change the window size followed by
		 * another one that does - and between the two, we get a
		 * display callback, which eats up the initial image we had
		 * set up via image_available().  So by default we wind up
		 * with a center of random data with an unsightly black border.
		 */
		glutReshapeWindow(glutGet(GLUT_SCREEN_WIDTH),
		    glutGet(GLUT_SCREEN_HEIGHT));

		glutFullScreen();
	} else {
		note("fullscreen mode does nothing with graphics disabled; "
		    "image is still %ldx%ld\n", Width, Height);
	}
}

/*
 * Let GLUT take control, and call our callbacks when necessary.
 */
void
window_mainloop(void)
{
	if (window_graphics()) {
		glutIdleFunc(redisplay_cb);
		glutReshapeFunc(reshape_cb);
		glutKeyboardFunc(keyboard_cb);
		glutMouseFunc(mouse_cb);
		glutMotionFunc(motion_cb);
		glutMainLoop();
	} else {
		for (;;) {
			window_step();
#if 0
			if (key = getchar()) {
				keyboard_cb(key, 0, 0);
			}
#endif
		}
	}
}
