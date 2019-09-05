/*
 * stroke.c - uses data from mouse input to generate "strokes" in the image.
 *
 * This uses an algorithm from Aubrey Jaffer's paper "Oseen Flow in Ink
 * Marbling" (https://arxiv.org/pdf/1702.02106.pdf).  The algorithm is
 * implemented in OpenCL.  Most of the code in this file is dedicated to
 * keeping a queue of mouse movements to use with the kernel.
 *
 * As described in the paper, we get nicer results if we break down larger
 * movements into a series of smaller ones (referred to here as "segments").
 */
#include <stdlib.h>
#include <strings.h>
#include <assert.h>
#include <math.h>

#include "common.h"

#include "debug.h"
#include "module.h"
#include "opencl.h"
#include "osdep.h"
#include "param.h"
#include "stroke.h"
#include "util.h"

/* ------------------------------------------------------------------ */

/*
 * Data describing a single linear movement.
 */
typedef struct stroke {
	pix_t		ox;		/* starting coordinates */
	pix_t		oy;
	pix_t		nx;		/* ending coordinates */
	pix_t		ny;
	int		nsegs_done;	/* number of segments stroked */
	int		nsegs_total;	/* number of segments to stroke */

	struct stroke	*next;
	struct stroke	*prev;
} stroke_t;

static struct {
	stroke_t	strokes;	/* head of linked list */

	kernel_data_t	kernel;

	param_id_t	viscid;		/* Viscosity parameter ID */
} Stroke;

/* ------------------------------------------------------------------ */

static void
stroke_preinit(void)
{
	param_init_t	pi;

	debug_register_toggle('s', "stroke processing", DB_STROKE, NULL);

	bzero(&pi, sizeof (pi));

	/*
	 * The parameter tracks the base-2 logarithm of the "viscosity" that
	 * the image is modeled as having.  The default value is 3, i.e. a
	 * viscosity of 8 (arbitrary units).
	 */
	pi.pi_min = 0;
	pi.pi_default = 3;
	pi.pi_max = 6;
	pi.pi_units = 1;		// converted in stroke_viscosity()
	pi.pi_ap_freq = AP_FREQ_OFF;
	pi.pi_ap_rate = AP_RATE_OFF;
	Stroke.viscid = param_register("stroke viscosity", &pi);

	param_key_register('{', KB_DEFAULT, Stroke.viscid, -1);
	param_key_register('}', KB_DEFAULT, Stroke.viscid,  1);
	param_key_register('[', KB_DEFAULT, Stroke.viscid, -1);
	param_key_register(']', KB_DEFAULT, Stroke.viscid,  1);
	param_key_register('/', KB_KEYPAD, Stroke.viscid, -1);
	param_key_register('*', KB_KEYPAD, Stroke.viscid,  1);
}

static void
stroke_init(void)
{
	Stroke.strokes.next = Stroke.strokes.prev = &Stroke.strokes;

	kernel_create(&Stroke.kernel, "stroke");
}

static void
stroke_fini(void)
{
	while (stroke_pending()) {
		stroke_t	*s = Stroke.strokes.next;

		s->next = s->next->next;
		mem_free((void **)&s);
	}

	kernel_cleanup(&Stroke.kernel);
}

const module_ops_t	stroke_ops = {
	stroke_preinit,
	stroke_init,
	stroke_fini
};

/* ------------------------------------------------------------------ */

/*
 * Is there at least one stroke pending?
 */
bool
stroke_pending(void)
{
	return (Stroke.strokes.next != &Stroke.strokes);
}

/*
 * How viscous should the image be in response to the stroke?
 * (This is what's called "L" in the paper.)
 */
static float
stroke_viscosity(void)
{
	return (0.01f * (float)(1 << param_int(Stroke.viscid)));
}

/*
 * Figure out how many segments to break this request into.
 */
static int
stroke_nsegs(pix_t ox, pix_t oy, pix_t nx, pix_t ny)
{
	const float	dx = (float)(spix_t)(nx - ox);
	const float	dy = (float)(spix_t)(ny - oy);
	const float	largedim = (float)(MAX(Width, Height));
	const float	len = sqrtf(dx * dx + dy * dy) / largedim;

	/*
	 * Section 8 of the paper suggests this formula for
	 * "[t]he number of segments for the computation".
	 * The paper assumes that the height of the image is 2.
	 */
	const int	nsegs = ((int)ceilf((len * 2.0) / stroke_viscosity()));

	/*
	 * Low viscosities yield large segment counts, and those can take a
	 * while to render. This is an attempt to balance getting a smooth
	 * stroke against getting closer to real-time response.
	 */
	return (MIN(nsegs, 20));
}

/*
 * Called by the mouse motion code to add a new stroke to the queue.
 */
void
stroke_add(pix_t ox, pix_t oy, pix_t nx, pix_t ny)
{
	stroke_t	*s;

	if (ox == nx && oy == ny) {
		debug(DB_STROKE, "Ignoring empty stroke: "
		    "[ %4zu, %4zu ] -> [ %4zu, %4zu ]\n", ox, oy, nx, ny);
		return;
	} else if (ox >= Width || nx >= Width || oy >= Height || ny >= Height) {
		debug(DB_STROKE, "Ignoring bad stroke: "
		    "[ %4zu, %4zu ] -> [ %4zu, %4zu ]\n", ox, oy, nx, ny);
		return;
	}

	/*
	 * If there is already at least one stroke pending that we haven't
	 * started work on, and the mouse movement being added starts at the
	 * end of the last stroke, relocate that stroke's end to be the end of
	 * this mouse movement.  This loses some of the detail of the mouse
	 * path, but it also helps make sure that the stroke code doesn't fall
	 * too far behind.
	 */
	if (stroke_pending()) {
		s = Stroke.strokes.prev;

		if (s->nx == ox && s->ny == oy &&
		    s->ox != ox && s->oy != oy &&
		    !(s->ox == nx && s->oy == ny) &&
		    s->nsegs_done == 0) {
			debug(DB_STROKE, "Merging strokes:\n");
			debug(DB_STROKE, "  [ %4zu, %4zu ] -> [ %4zu, %4zu ]\n",
			    s->ox, s->oy, s->nx, s->ny);
			debug(DB_STROKE, "+ [ %4zu, %4zu ] -> [ %4zu, %4zu ]\n",
			    s->nx, s->ny, nx, ny);
			debug(DB_STROKE, "= [ %4zu, %4zu ] -> [ %4zu, %4zu ]\n",
			    s->ox, s->oy, nx, ny);

			s->nx = nx;
			s->ny = ny;
			s->nsegs_total = stroke_nsegs(s->ox, s->oy, nx, ny);
			return;
		}
	}

	/*
	 * No strokes to merge with, so add a new one.
	 */
	s = mem_alloc(sizeof (stroke_t));

	s->ox = ox;
	s->oy = oy;
	s->nx = nx;
	s->ny = ny;
	s->nsegs_done = 0;
	s->nsegs_total = stroke_nsegs(ox, oy, nx, ny);

	s->next = &Stroke.strokes;
	s->prev = Stroke.strokes.prev;
	s->prev->next = s;
	s->next->prev = s;
}

/*
 * Fetch a piece of work to do.
 */
static void
stroke_fetch(pix_t *oxp, pix_t *oyp, pix_t *nxp, pix_t *nyp)
{
	stroke_t	*s = Stroke.strokes.next;
	const spix_t	dx = s->nx - s->ox;
	const spix_t	dy = s->ny - s->oy;
	const int	done = s->nsegs_done;
	const int	total = s->nsegs_total;

	assert(stroke_pending());
	assert(done < total);

	/*
	 * Pick the next segment of the current stroke.
	 */
	*oxp = s->ox + (dx * done) / total;
	*oyp = s->oy + (dy * done) / total;
	*nxp = s->ox + (dx * (done + 1)) / total;
	*nyp = s->oy + (dy * (done + 1)) / total;

	s->nsegs_done++;
	if (s->nsegs_done == s->nsegs_total) {
		s->next->prev = s->prev;
		s->prev->next = s->next;
		mem_free((void **)&s);
	}

	debug(DB_STROKE, "Stroke: [ %4zu, %4zu ] -> [ %4zu, %4zu ] [%d/%d]\n",
	    *oxp, *oyp, *nxp, *nyp, done + 1, total);
}

/*
 * This operates on data from datasrc.c - i.e. image2d_t's of data with
 * each vector component in the range [0, 1].  It doesn't know anything
 * about the meaning of the data; it simply assumes that it's meaningful
 * to perform the stroke algorithm on it.
 */
void
stroke_step(cl_mem srcdata, cl_mem dstdata)
{
	kernel_data_t	*const	kd = &Stroke.kernel;
	float			viscosity = stroke_viscosity();
	pix_t			ox, oy, nx, ny;
	int			arg;
	hrtime_t		a, b;

	if (debug_enabled(DB_STROKE) && debug_enabled(DB_PERF)) {
		kernel_wait();
		a = gethrtime();
	}

	/*
	 * Get a piece of work to do.
	 */
	stroke_fetch(&ox, &oy, &nx, &ny);

	arg = 0;
	kernel_setarg(kd, arg++, sizeof (pix_t), &Width);
	kernel_setarg(kd, arg++, sizeof (pix_t), &Height);
	kernel_setarg(kd, arg++, sizeof (pix_t), &ox);
	kernel_setarg(kd, arg++, sizeof (pix_t), &oy);
	kernel_setarg(kd, arg++, sizeof (pix_t), &nx);
	kernel_setarg(kd, arg++, sizeof (pix_t), &ny);
	kernel_setarg(kd, arg++, sizeof (float),  &viscosity);
	kernel_setarg(kd, arg++, sizeof (cl_mem), &srcdata);
	kernel_setarg(kd, arg++, sizeof (cl_mem), &dstdata);

	kernel_invoke(kd, 2, NULL, NULL);

	if (debug_enabled(DB_STROKE) && debug_enabled(DB_PERF)) {
		kernel_wait();
		b = gethrtime();

		debug(DB_STROKE, "Stroke: [ %4zu, %4zu ] -> [ %4zu, %4zu ]: "
			"%6llu usec\n", ox, oy, nx, ny, (b - a) / 1000);
	}
}
