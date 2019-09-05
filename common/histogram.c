/*
 * histogram.c - display a simple ASCII-art histogram of the data.
 *
 * The core algorithm generates an "image" that's really a set of
 * multi-dimensional data points.  It has a render() routine to turn those
 * into a pretty picture, but it can be useful to visualize the data points
 * more directly.  This is somewhat like the heatmap code, but simpler --
 * it generates a histogram of each individual axis of the data.  This can
 * be useful as a debugging tool when trying out new algorithms, to see
 * where data is clustering and how it can be tweaked into something that
 * looks interesting.
 */
#include <strings.h>

#include "common.h"
#include "opencl.h"
#include "debug.h"
#include "keyboard.h"
#include "module.h"
#include "util.h"

/* ------------------------------------------------------------------ */

static struct {
	cl_datavec	*cpu_buf;
} Histogram;

/* ------------------------------------------------------------------ */

static void
histogram_init(void)
{
	if (debug_enabled(DB_HISTO)) {
		const size_t	arraysize =
		    (size_t)Width * Height * sizeof (cl_datavec);
		Histogram.cpu_buf = mem_alloc(arraysize);
	}
}

static void
histogram_fini(void)
{
	if (debug_enabled(DB_HISTO)) {
		mem_free((void **)&Histogram.cpu_buf);
	}
}

static void
histogram_toggle(void)
{
	if (debug_enabled(DB_HISTO)) {
		histogram_init();
	} else {
		histogram_fini();
	}
}

static void
histogram_preinit(void)
{
	debug_register_toggle('H', "histogram", DB_HISTO, histogram_toggle);
}

const module_ops_t	histogram_ops = {
	histogram_preinit,
	histogram_init,
	histogram_fini
};

/* ------------------------------------------------------------------ */

#define	HGCOLS	80
#define	HGROWS	10

/*
 * Given a data point, figure out which bucket it belongs in.
 *
 * We're examining the "dim" component of the vector, and we're assuming
 * that it has a value between "min" and "max".  There are "nbuck" buckets
 * in the histogram.
 */
static int
bucket(const cl_datavec *datum, int dim, float min, float max, int nbuck)
{
	int	b;

	b = (int)(((datum->s[dim] - min) / (max - min)) * (nbuck - 1));

	if (b < 0) {
		b = 0;
	} else if (b > nbuck - 1) {
		b = nbuck - 1;
	}

	return (b);
}

static void
histogram(const char *name,
    const cl_datavec *data, pix_t width, pix_t height,
    int dim, float min, float max)
{
	int	buckets[HGCOLS];
	char	output[HGCOLS + 2], *p;
	int	b, i;
	int	maxb;

	debug(DB_HISTO, "%s:\n", name);

	/*
	 * This is one of the only O(data) operations that I haven't
	 * bothered pushing out to the GPU, since this doesn't seem to be
	 * performance-critical.
	 */
	bzero(buckets, sizeof (buckets));
	for (pix_t p = 0; p < width * height; p++) {
		b = bucket(&data[p], dim, min, max, HGCOLS);
		buckets[b]++;
	}
	maxb = 0;
	for (b = 0; b < HGCOLS; b++) {
		if (buckets[b] > maxb) {
			maxb = buckets[b];
		}
	}

	for (i = HGROWS - 1; i >= 0; i--) {
		p = output;
		for (b = 0; b < HGCOLS; b++) {
			*p++ = ((buckets[b] > (i * maxb) / HGROWS) ? '*' : ' ');
		}
		*p++ = '\n';
		*p++ = '\0';
		debug(DB_HISTO, output);
	}

	p = output;
	for (b = 0; b < HGCOLS; b++) {
		*p++ = ('0' + (b / 10));
	}
	*p++ = '\n';
	*p++ = '\0';
	debug(DB_HISTO, output);

	p = output;
	for (b = 0; b < HGCOLS; b++) {
		*p++ = ('0' + (b % 10));
	}
	*p++ = '\n';
	*p++ = '\0';
	debug(DB_HISTO, output);
}

/*
 * Generate 1-D text histograms of the data.
 */
void
histogram_display(cl_mem buf, float min, float max)
{
	if (!debug_enabled(DB_HISTO)) {
		return;
	}

	ocl_image_readfromgpu(buf, Histogram.cpu_buf, Width, Height);

	for (int ch = 0; ch < DATA_DIMENSIONS; ch++) {
		char	name[2] = { "xyzw"[ch], '\0' };

		histogram(name, Histogram.cpu_buf, Width, Height, ch, min, max);
	}

	debug(DB_HISTO, "\n");
}
