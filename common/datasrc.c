/*
 * datasrc.c - chooses between multiple data sources (the core algorithm,
 * input from mouse movement, and new images) to get a new image to display.
 * It also offers a hook to update the image before it is displayed, e.g. by
 * shifting to heatmap mode.
 *
 * This keeps a copy of the image as an array of cl_datavec's with all
 * components in the range [-1, 1].  This code doesn't even try to interpret
 * that array; it uses (*Datasrc.ops->render)() to turn it into an RGB image.
 */
#include <assert.h>
#include <math.h>

#include "common.h"

#include "core.h"
#include "datasrc.h"
#include "debug.h"
#include "heatmap.h"
#include "histogram.h"
#include "image.h"
#include "module.h"
#include "opencl.h"
#include "param.h"
#include "stroke.h"
#include "util.h"

/* ------------------------------------------------------------------ */

typedef struct step_cb {
	struct step_cb	*next;
	void		(*cb)(void *);
	void		*cbarg;
	int		when;
} step_cb_t;

#define	NRENDERED	2
static struct {
	cl_mem		rendered[NRENDERED];	/* The last image rendered */
	int		last;			/* which rendered[] to use */
	int		steps;			/* number of core steps taken */
	core_ops_t	*ops;
	step_cb_t	*cblist;		/* list of callbacks to run */
} Datasrc;

/* ------------------------------------------------------------------ */

static void
datasrc_init(void)
{
	int	i;

	/*
	 * The stroke kernel relies heavily on the hardware's ability to do
	 * sub-pixel sampling.  It can only do this using OpenCL images with
	 * "normalized" (CL_FLOAT) coordinates.  So I've made the datasrc
	 * module use the same kind of image that the stroke module needs,
	 * to avoid needing to do an extra copy in stroke processing.
	 */
	for (i = 0; i < NRENDERED; i++) {
		Datasrc.rendered[i] = ocl_datavec_image_create(Width, Height);
	}
	Datasrc.last = 0;
	Datasrc.steps = 0;
	Datasrc.cblist = NULL;
}

static void
datasrc_fini(void)
{
	int	i;

	for (i = 0; i < NRENDERED; i++) {
		buffer_free(&Datasrc.rendered[i]);
	}
}

const module_ops_t	datasrc_ops = {
	NULL,
	datasrc_init,
	datasrc_fini
};

/* ------------------------------------------------------------------ */

/*
 * Having this layer of indirection isn't all that important for now,
 * but it offers a hook for allowing multiple core operations to coexist
 * at some point in the future.
 */
void
core_ops_register(core_ops_t *ops)
{
	assert(Datasrc.ops == NULL);
	Datasrc.ops = ops;
}

void
core_ops_unregister(core_ops_t *ops)
{
	assert(Datasrc.ops == ops);
	Datasrc.ops = NULL;
}

/* ------------------------------------------------------------------ */

/*
 * Register a callback to be called in a certain number of steps
 * from now.
 */
void
datasrc_step_registercb(int nsteps, void (*cb)(void *), void *arg)
{
	step_cb_t *scb;

	scb = mem_alloc(sizeof (step_cb_t));
	scb->cb = cb;
	scb->cbarg = arg;
	scb->when = Datasrc.steps + nsteps;
	scb->next = Datasrc.cblist;
	Datasrc.cblist = scb;
}

static void
datasrc_step_taken(void)
{
	step_cb_t **scbpp;

	Datasrc.steps++;

	for (scbpp = &Datasrc.cblist; *scbpp != NULL; ) {
		step_cb_t *scb = *scbpp;

		if (scb->when == Datasrc.steps) {
			(*scb->cb)(scb->cbarg);
			*scbpp = scb->next;
			mem_free((void **)&scb);
		} else {
			scbpp = &(*scbpp)->next;
		}
	}
}

/* ------------------------------------------------------------------ */

/*
 * This is called to preserve the current image prior to a window resize
 * operation.  It explicitly *doesn't* invoke the heatmap code, since the
 * resulting image will be "unrendered" and passed back into the core
 * after the resize takes place.
 */
void
datasrc_rerender(cl_mem image)
{
	cl_mem	src = Datasrc.rendered[Datasrc.last];

	if (Datasrc.ops == NULL) {
		die("No core algorithm registered!\n");
	}
	(*Datasrc.ops->render)(src, image);
}

/*
 * Generate the next image.  "image" is an image2d_t of RGBA floats.
 */
void
datasrc_step(cl_mem image)
{
	const float		min = (*Datasrc.ops->min)();
	const float		max = (*Datasrc.ops->max)();
	const datavec_shape_t	shape = (*Datasrc.ops->datavec_shape)();
	cl_mem			data;
	bool			step_taken;

	data = Datasrc.rendered[Datasrc.last];

	step_taken = false;
	if (Datasrc.ops == NULL) {
		die("No core algorithm registered!\n");
	}
	if (image_available(image)) {
		/*
		 * There was an image to load, and we loaded it.
		 *
		 * We need to invoke unrender() to transform the image into
		 * data that the core code can operate on.
		 */
		(*Datasrc.ops->unrender)(image, data);

		/*
		 * Import the unrendered data back into the core.
		 */
		(*Datasrc.ops->import)(data);
	} else if (stroke_pending()) {
		cl_mem	newdata;

		/*
		 * There are one or more mouse strokes pending.
		 * Operate on them.
		 */
		while (stroke_pending()) {
			Datasrc.last = (Datasrc.last + 1) % NRENDERED;
			newdata = Datasrc.rendered[Datasrc.last];
			stroke_step(data, newdata);
			data = newdata;
		}

		/*
		 * Once those are done, import the stroked data back to the
		 * core algorithm, and generate an RGBA image of the result.
		 */
		(*Datasrc.ops->import)(data);

		(*Datasrc.ops->render)(data, image);
	} else {
		/*
		 * No images, no strokes; just do a regular step.
		 * First run the autopilot to advance parameters if needed.
		 */
		autopilot_step();
		(*Datasrc.ops->step_and_export)(data);

		/*
		 * Generate the RGBA image.
		 */
		(*Datasrc.ops->render)(data, image);

		step_taken = true;
	}

	/*
	 * Display text histograms if desired.
	 */
	histogram_display(data, min, max);

	/*
	 * Add a heatmap to the image if desired.
	 * This only updates the RGBA image; Datasrc.rendered is unmodified.
	 */
	heatmap_update(data, min, max, shape, image);

	if (step_taken) {
		datasrc_step_taken();
	}

	/*
	 * Wait for all OpenCL work to finish before returning.
	 */
	kernel_wait();
}
