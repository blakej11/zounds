/*
 * image.c - loads new images from other data sources, and stores the current
 * image to a PPM file.
 *
 * This operates on uint8_t RGBA data.  Many data sources have to convert
 * from other formats; image_copy() and its callbacks enable that.
 */
#include <assert.h>
#include <strings.h>

#include "common.h"

#include "camera.h"
#include "debug.h"
#include "image.h"
#include "keyboard.h"
#include "module.h"
#include "opencl.h"
#include "ppm.h"
#include "randbj.h"
#include "template.h"
#include "util.h"
#include "window.h"

/* ------------------------------------------------------------------ */

/*
 * Number of bytes per pixel in the image.  It is 4 because
 * window_init()/texture_init() creates it as RGBA / UNSIGNED_BYTE.
 */
#define	IMAGE_BPP		(4 * sizeof (char))

typedef enum {
	LOAD_NONE = 0,
	LOAD_DATAFILE,
	LOAD_CAMERA,
	LOAD_RANDOM,
	LOAD_OLDIMAGE
} loadstate_t;

static struct {
	loadstate_t	loadstate;	/* anything to load? */
	char		*datafile;	/* for LOAD_DATAFILE */
	template_t	*template;	/* for saving images */

	/*
	 * For keeping an image around across resizes.
	 *
	 * This is the only O(datasize) data that's kept around across
	 * a fini()/resize()/init() operation.
	 */
	uint8_t		*old_rgba;
	pix_t		old_width;
	pix_t		old_height;
} Image;

/* ------------------------------------------------------------------ */

/* This gets called from main() before image_preinit(). */
void
image_datafile(char *file)
{
	Image.loadstate = LOAD_DATAFILE;
	Image.datafile = file;
	window_update();
}

static void
set_loadstate_camera(void)
{
	Image.loadstate = LOAD_CAMERA;
	window_update();
}

static void
set_loadstate_random(void)
{
	Image.loadstate = LOAD_RANDOM;
	window_update();
}

static void
image_preinit(void)
{
	if (Image.loadstate != LOAD_DATAFILE) {
		Image.loadstate = LOAD_RANDOM;
	}

	key_register('c', KB_DEFAULT, "initialize data using camera",
	    set_loadstate_camera);
	key_register('r', KB_DEFAULT, "fill with random data",
	    set_loadstate_random);
	key_register('0', KB_KEYPAD, "initialize data using camera",
	    set_loadstate_camera);
	key_register('3', KB_KEYPAD, "fill with random data",
	    set_loadstate_random);

	debug_register_toggle('I', "image I/O", DB_IMAGE, NULL);
}

const module_ops_t	image_ops = {
	image_preinit
};

/* ------------------------------------------------------------------ */

/*
 * Called to preserve the current image before a resize.
 */
void
image_preserve(pix_t width, pix_t height, cl_mem image)
{
	const size_t	rgba_size = (size_t)width * height * IMAGE_BPP;
	uint8_t		*rgba;

	/*
	 * This can happen if we are initialized with a data file that's
	 * larger than we can display.  The resize callback will get called
	 * to shrink our image, but since we haven't actually read in the
	 * image from the file yet, we don't want to preserve it.
	 */
	if (Image.loadstate == LOAD_DATAFILE) {
		return;
	}

	rgba = mem_alloc(rgba_size);
	ocl_image_readfromgpu(image, rgba, width, height);

	/*
	 * The image lives here across the resize.
	 */
	Image.old_width = width;
	Image.old_height = height;
	Image.old_rgba = rgba;
	Image.loadstate = LOAD_OLDIMAGE;
}

/*
 * No translation needed here.  We just use this to take advantage of
 * image_copy()'s ability to handle image resizing.
 */
static void
rgba_to_rgba(const uint8_t *oi, uint8_t *ni, pix_t op, pix_t np)
{
	ni[IMAGE_BPP * np + 0] = oi[IMAGE_BPP * op + 0];
	ni[IMAGE_BPP * np + 1] = oi[IMAGE_BPP * op + 1];
	ni[IMAGE_BPP * np + 2] = oi[IMAGE_BPP * op + 2];
	ni[IMAGE_BPP * np + 3] = oi[IMAGE_BPP * op + 3];
}

/*
 * Load a previously preserved image.
 */
static bool
load_oldimage_cb(pix_t width, pix_t height, uint8_t *rgba)
{
	assert(Image.old_rgba != NULL);

	verbose(DB_IMAGE, "Loading a previously rendered image\n");

	/*
	 * Width and Height may have changed compared to the old image,
	 * in which case image_copy() won't touch all parts of the new
	 * image.
	 */
	bzero(rgba, width * height * IMAGE_BPP);

	image_copy(Image.old_width, Image.old_height, Image.old_rgba,
	    width, height, rgba, rgba_to_rgba);

	mem_free((void **)&Image.old_rgba);
	Image.old_width = Image.old_height = 0;

	return (true);
}

/*
 * Convert from three-bytes-per-pixel data, such as what comes from a PPM
 * file, to four-bytes-per-pixel RGBA data.
 */
static void
rgb_to_rgba(const uint8_t *rgb, uint8_t *rgba, pix_t op, pix_t np)
{
	rgba[4 * np + 0] = rgb[3 * op + 0];
	rgba[4 * np + 1] = rgb[3 * op + 1];
	rgba[4 * np + 2] = rgb[3 * op + 2];
	rgba[4 * np + 3] = 0;
}

/*
 * Load from a PPM file.
 *
 * We only support raw files (with header "P6"), and not all-text PPM files
 * (with header "P3").
 */
static bool
load_file_cb(pix_t width, pix_t height, uint8_t *rgba)
{
	bool		rv;
	pix_t		iw, ih;
	uint8_t		*rgb;

	assert(Image.datafile != NULL);

	verbose(DB_IMAGE, "Loading from \"%s\"\n", Image.datafile);

	rv = ppm_read_sizes(Image.datafile, &iw, &ih);
	if (rv) {
		rgb = mem_alloc(iw * ih * 3 * sizeof (uint8_t));
		rv = ppm_read_rgb(Image.datafile, iw, ih, rgb);
		if (rv) {
			image_copy(iw, ih, rgb, width, height, rgba,
			    rgb_to_rgba);
		}
		mem_free((void **)&rgb);
	}

	Image.datafile = NULL;
	return (rv);
}

/*
 * Apparently it's common for webcam-like things to return their results
 * in BGR order rather than RGB.  Thankfully, image_copy() makes it
 * pretty easy to handle that.
 */
static void
bgr_to_rgba(const uint8_t *bgr, uint8_t *rgba, pix_t op, pix_t np)
{
	rgba[4 * np + 0] = bgr[3 * op + 2];
	rgba[4 * np + 1] = bgr[3 * op + 1];
	rgba[4 * np + 2] = bgr[3 * op + 0];
	rgba[4 * np + 3] = 0;
}

/*
 * Load an image from the camera.
 */
static bool
load_camera_cb(pix_t width, pix_t height, uint8_t *rgba)
{
	const bool	inited = camera_initialized();
	uint8_t		*bgr;
	bool		rv;

	if (!inited && !camera_init()) {
		return (false);
	}

	verbose(DB_IMAGE, "Loading image from camera\n");

	if (!camera_grab()) {
		return (false);
	}

	bgr = camera_retrieve();
	if (bgr != NULL) {
		bzero(rgba, width * height * 4 * sizeof (char));
		image_copy(camera_width(), camera_height(), bgr,
		    width, height, rgba, bgr_to_rgba);
	}

	if (!inited) {
		camera_fini();
	}
	return (bgr != NULL);
}

/*
 * Get random noise.
 */
static bool
load_random_cb(pix_t width, pix_t height, uint8_t *rgba)
{
	verbose(DB_IMAGE, "Loading random data\n");

	for (pix_t p = 0; p < width * height; p++) {
		rgba[IMAGE_BPP * p + 0] = drandbj() * 255.0;
		rgba[IMAGE_BPP * p + 1] = drandbj() * 255.0;
		rgba[IMAGE_BPP * p + 2] = drandbj() * 255.0;
		rgba[IMAGE_BPP * p + 3] = drandbj() * 255.0;
	}

	return (true);
}

/*
 * Checks whether there is an image available from some other source.
 * If there is, it loads it into the passed-in OpenCL image.
 */
bool
image_available(cl_mem image)
{
	const size_t	rgba_size = (size_t)Width * Height * IMAGE_BPP;
	bool		(*cb)(pix_t, pix_t, uint8_t *);
	uint8_t		*rgba;
	bool		rv;

	switch (Image.loadstate) {
	case LOAD_NONE:
		return (false);		/* nothing to do */
		break;
	case LOAD_DATAFILE:
		cb = load_file_cb;
		break;
	case LOAD_CAMERA:
		cb = load_camera_cb;
		break;
	case LOAD_RANDOM:
		cb = load_random_cb;
		break;
	case LOAD_OLDIMAGE:
		cb = load_oldimage_cb;
		break;
	default:
		assert(0 && "bad load state");
		break;
	}

	rgba = mem_alloc(rgba_size);
	rv = (*cb)(Width, Height, rgba);
	if (rv) {
		ocl_image_writetogpu(rgba, image, Width, Height);
	}
	mem_free((void **)&rgba);

	Image.loadstate = LOAD_NONE;
	return (rv);
}

/*
 * Data written to a PPM file is only three bytes per pixel; this
 * discards the fourth byte.
 */
static void
rgba_to_rgb(const uint8_t *rgba, uint8_t *rgb, pix_t op, pix_t np)
{
	rgb[3 * np + 0] = rgba[4 * op + 0];
	rgb[3 * np + 1] = rgba[4 * op + 1];
	rgb[3 * np + 2] = rgba[4 * op + 2];
}

void
image_save(cl_mem image, int steps)
{
	const size_t	rgba_size = (size_t)Width * Height * IMAGE_BPP;
	const size_t	rgb_size =
	    (size_t)Width * Height * 3 * sizeof (uint8_t);
	uint8_t		*rgba, *rgb;
	const char	*filename;

	/*
	 * Get a useful filename, based on the number of images rendered
	 * since the start of the program.
	 */
	if (Image.template == NULL) {
		Image.template = template_alloc("images");
	}
	filename = template_name(Image.template, NULL, steps);

	rgba = mem_alloc(rgba_size);
	rgb = mem_alloc(rgb_size);

	ocl_image_readfromgpu(image, rgba, Width, Height);
	image_copy(Width, Height, rgba, Width, Height, rgb, rgba_to_rgb);
	ppm_write_rgb(filename, rgb, Width, Height);

	mem_free((void **)&rgb);
	mem_free((void **)&rgba);

	verbose(DB_IMAGE, "Saved image %05d\n", steps);
}

/*
 * The workhorse for translating and resizing images.
 *
 * This takes an old image ("oi"), with size "ow" x "oh", and copies what
 * parts of it that it can into a new image ("ni"), with size "nw" x "nh".
 * The images are arrays of uint8_t's, and the specified sizes are in
 * pixels (not bytes).  The copy callback function "copy_cb" is called for
 * every pixel of the old image that has a place in the new image.
 *
 * This does not rescale the old image to fit in the new one.  It truncates
 * and pads along the horizontal and/or vertical axes as needed.  It
 * centers the old image within the new image.
 */
void
image_copy(const pix_t ow, const pix_t oh, const uint8_t *oi,
    const pix_t nw, const pix_t nh, uint8_t *ni,
    void (*copy_cb)(const uint8_t *oi, uint8_t *ni, pix_t op, pix_t np))
{
	const pix_t	odx = (nw < ow ? (ow - nw) / 2 : 0);
	const pix_t	ndx = (nw > ow ? (nw - ow) / 2 : 0);
	const pix_t	ody = (nh < oh ? (oh - nh) / 2 : 0);
	const pix_t	ndy = (nh > oh ? (nh - oh) / 2 : 0);
	const pix_t	w = MIN(ow, nw);
	const pix_t	h = MIN(oh, nh);

	debug(DB_IMAGE, "old <%4zu,%4zu>: x = [%4zu, %4zu), y = [%4zu, %4zu)\n",
	    ow, oh, odx, odx + w, ody, ody + h);
	debug(DB_IMAGE, "new <%4zu,%4zu>: x = [%4zu, %4zu), y = [%4zu, %4zu)\n",
	    nw, nh, ndx, ndx + w, ndy, ndy + h);

	for (pix_t y = 0; y < h; y++) {
		for (pix_t x = 0; x < w; x++) {
			const pix_t	op = (y + ody) * ow + (x + odx);
			const pix_t	np = (y + ndy) * nw + (x + ndx);

			(*copy_cb)(oi, ni, op, np);
		}
	}
}
