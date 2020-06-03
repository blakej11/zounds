/*
 * tweak.c - holds the policy parts of the MSTP algorithm.
 * These are the areas that are probably most interesting to tweak.
 */

#include <stdlib.h>
#include <strings.h>
#include <math.h>
#include <assert.h>
#include <limits.h>

#include "common.h"
#include "datasrc.h"
#include "keyboard.h"
#include "window.h"
#include "param.h"
#include "tweak.h"

/* ------------------------------------------------------------------ */

/*
 * The tweakable parameters, and their default values.
 */
static const param_init_t Param_values[] = {
     /* min,     def,     max,  units,    freq,    rate,  abbr, name */
	{ 2, NSCALES, NSCALES,      1, APF_LOW, APR_MED,  "NS", "nscales" },
	// Number of scales to use.

	{ 0,       4,       6,      1, APF_MED, APR_MED,  "SP", "speed" },
	// log2(scale factor) for multiscale adj[]
	// -1 = stop updates (still lets the heatmap spin)

	{ 1,       2,       3,      1, APF_LOW, APR_LOW,  "NB", "nbox" },
	// Number of box blur passes to do.

#define	NADJ	(NADJTYPE - 1)
	{ 0,       0,    NADJ,      1, APF_LOW, APR_HIGH, "AT", "adjtype" },
	// Which array of adjustments to use.
#undef	NADJ

	{ 0,       0,     999,      1, APF_OFF, APR_HIGH, "RT", "rendertype" },
	// Which style of rendering to use.
};

/* ------------------------------------------------------------------ */

/*
 * The parameter IDs, used for calling into the param subsystem.
 */
static struct {
	param_id_t	nscales;
	param_id_t	speed;
	param_id_t	nbox;
	param_id_t	adjtype;
	param_id_t	rendertype;
} Params;

static void
key_preset_5_cb(void *arg)
{
	param_set_int(Params.nscales, 9);
	param_set_int(Params.speed, 0);
}

static void
key_preset_6_cb(void *arg)
{
	param_set_int(Params.nscales, 2);
	param_set_int(Params.speed, 0);
	param_set_int(Params.nbox, 2);
}

/*
 * Flip to a few preset points in parameter space.
 */
static void
key_preset(int arg)
{
	if (arg == 1) {				/* default */
		param_reset_to_defaults();
	} else if (arg == 2) {			/* sketchable */
		param_undump("aNS2SP0");
	} else if (arg == 3) {			/* ravioli */
		param_undump("aNS6SP2AT5");
	} else if (arg == 4) {			/* circles */
		param_undump("aNS5SP4AT6");
	} else if (arg == 5) {			/* complexity fade-in */
		param_undump("aNS2SP6AT5");
		datasrc_step_registercb(40, key_preset_5_cb, NULL);
	} else if (arg == 6) {			/* complexity fade-out */
		param_undump("aNS9SP6NB3AT1");
		datasrc_step_registercb(15, key_preset_6_cb, NULL);
	}
}

void
tweak_preinit(void)
{
	param_register_table(Param_values,
	    sizeof (Param_values) / sizeof (*Param_values));

	Params.nscales = param_lookup("nscales");
	param_key_register('<', KB_DEFAULT, Params.nscales, -1);
	param_key_register(',', KB_DEFAULT, Params.nscales, -1);
	param_key_register('>', KB_DEFAULT, Params.nscales,  1);
	param_key_register('.', KB_DEFAULT, Params.nscales,  1);
	param_key_register('9', KB_KEYPAD, Params.nscales, -1);
	param_key_register('-', KB_KEYPAD, Params.nscales,  1);

	Params.speed = param_lookup("speed");
	param_key_register('-', KB_DEFAULT, Params.speed, -1);
	param_key_register('_', KB_DEFAULT, Params.speed, -1);
	param_key_register('+', KB_DEFAULT, Params.speed,  1);
	param_key_register('=', KB_DEFAULT, Params.speed,  1);
	param_key_register('6', KB_KEYPAD, Params.speed, -1);
	param_key_register('+', KB_KEYPAD, Params.speed,  1);

	Params.nbox = param_lookup("nbox");
	param_key_register('b', KB_DEFAULT, Params.nbox, -1);
	param_key_register('B', KB_DEFAULT, Params.nbox,  1);

	Params.adjtype = param_lookup("adjtype");
	param_key_register('j', KB_DEFAULT, Params.adjtype, -1);
	param_key_register('J', KB_DEFAULT, Params.adjtype,  1);

	Params.rendertype = param_lookup("rendertype");
	param_key_register('n', KB_DEFAULT, Params.rendertype, -1);
	param_key_register('N', KB_DEFAULT, Params.rendertype,  1);

	key_register_arg('7', KB_KEYPAD, "preset 1", key_preset, 1);
	key_register_arg('8', KB_KEYPAD, "preset 2", key_preset, 2);
	key_register_arg('4', KB_KEYPAD, "preset 3", key_preset, 3);
	key_register_arg('5', KB_KEYPAD, "preset 4", key_preset, 4);
	key_register_arg('1', KB_KEYPAD, "preset 5", key_preset, 5);
	key_register_arg('2', KB_KEYPAD, "preset 6", key_preset, 6);
}

void
tweak_init(void)
{
	/* tweak_multiscale_adj() uses adjtype and speed. */
	param_cb_register(Params.adjtype, multiscale_adjust);
	param_cb_register(Params.speed, multiscale_adjust);
}

void
tweak_fini(void)
{
	param_cb_unregister(Params.adjtype, multiscale_adjust);
	param_cb_unregister(Params.speed, multiscale_adjust);
}

/* ------------------------------------------------------------------ */

int
tweak_nscales(void)
{
	return (param_int(Params.nscales));
}

int
tweak_nbox(void)
{
	return (param_int(Params.nbox));
}

int
tweak_rendertype(void)
{
	return (param_int(Params.rendertype));
}

/*
 * Given a scale in the range [0, NSCALES), return a radius for the box blur.
 */
pix_t
tweak_box_radius(int scale)
{
	/*
	 * These values look nice and perform well on my box blur.
	 */
	const pix_t	scales[NSCALES] = {
		256, 144, 80, 48, 24, 12, 6, 3, 1
	};

	assert(scale >= 0 && scale < NSCALES);

	return ((pix_t)(window_getscale() * scales[scale]));
}

/*
 * This determines how the multiscale algorithm should adjust a pixel.
 *
 * Different rows in the adj[] array give visually different results;
 * a row is chosen based on the value of the "adjtype" parameter.
 * The multiscale algorithm chooses a column based on which pair of
 * scales had the smallest difference between box blurs.  (It uses the
 * smaller scale index of the pair.)
 *
 * Larger adjustment values cause the data point to move more.  So the
 * algorithm can be made to go faster by multiplying each adjustment
 * value by a constant; this is what the "speed" parameter does.
 */
float
tweak_multiscale_adj(int scale)
{
	const int	adjtype = param_int(Params.adjtype);
	const int	speed = param_int(Params.speed);

	/*
	 * The adjustments used at various scales.
	 */
	const int	adj[NADJTYPE][NSCALES - 1] = {
		{ 8, 7, 6, 5, 4, 3, 2, 1 },
		{ 1, 2, 3, 4, 5, 6, 7, 8 },
		{ 8, 4, 2, 2, 1, 1, 1, 1 },
		{ 1, 1, 1, 1, 2, 2, 4, 8 },
		{ 8, 3, 3, 3, 3, 3, 3, 3 },
		{ 8, 1, 1, 1, 1, 8, 8, 8 },	/* bigger blobs */
		{ 8, 8, 1, 1, 1, 1, 8, 8 },	/* smaller blobs */
	};

	assert(scale >= 0 && scale < NSCALES - 1);

	if (speed < 0) {
		return (0.0f);
	} else {
		return ((adj[adjtype][scale] << speed) * 0.001f);
	}
}
