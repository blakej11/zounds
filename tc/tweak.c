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
 * The tweakable parameters.
 */
static struct {
	param_id_t	nscales;
	param_id_t	speed;
	param_id_t	nbox;
	param_id_t	adjtype;
	param_id_t	rendertype;
} Params;

/* ------------------------------------------------------------------ */

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
	switch (arg) {
	case 1:	/* default */
		param_set_int(Params.nscales, 9);
		param_set_int(Params.speed, 4);
		param_set_int(Params.nbox, 2);
		param_set_int(Params.adjtype, 0);
		break;

	case 2:	/* sketchable */
		param_set_int(Params.nscales, 2);
		param_set_int(Params.speed, 0);
		param_set_int(Params.nbox, 2);
		param_set_int(Params.adjtype, 0);
		break;

	case 3:	/* ravioli */
		param_set_int(Params.nscales, 6);
		param_set_int(Params.speed, 2);
		param_set_int(Params.nbox, 2);
		param_set_int(Params.adjtype, 5);
		break;

	case 4:	/* circles */
		param_set_int(Params.nscales, 5);
		param_set_int(Params.speed, 4);
		param_set_int(Params.nbox, 2);
		param_set_int(Params.adjtype, 6);
		break;

	case 5:	/* complexity fade-in */
		param_set_int(Params.nscales, 2);
		param_set_int(Params.speed, 6);
		param_set_int(Params.nbox, 2);
		param_set_int(Params.adjtype, 5);
		datasrc_step_registercb(40, key_preset_5_cb, NULL);
		break;

	case 6:	/* complexity fade-out */
		param_set_int(Params.nscales, 9);
		param_set_int(Params.speed, 6);
		param_set_int(Params.nbox, 3);
		param_set_int(Params.adjtype, 1);
		datasrc_step_registercb(15, key_preset_6_cb, NULL);
		break;

	default:
		break;
	}
}

void
tweak_preinit(void)
{
	param_init_t	pi;

	bzero(&pi, sizeof (pi));

	/*
	 * Number of scales to use.
	 */
	pi.pi_min = 2;
	pi.pi_default = NSCALES;
	pi.pi_max = NSCALES;
	pi.pi_units = 1;
	pi.pi_ap_freq = AP_FREQ_LOW;
	pi.pi_ap_rate = AP_RATE_MED;
	Params.nscales = param_register("nscales", &pi);

	param_key_register('<', KB_DEFAULT, Params.nscales, -1);
	param_key_register(',', KB_DEFAULT, Params.nscales, -1);
	param_key_register('>', KB_DEFAULT, Params.nscales,  1);
	param_key_register('.', KB_DEFAULT, Params.nscales,  1);
	param_key_register('9', KB_KEYPAD, Params.nscales, -1);
	param_key_register('-', KB_KEYPAD, Params.nscales,  1);

	/*
	 * log2(scale factor) for multiscale adj[]
	 * -1 = stop updates (still lets the heatmap spin)
	 */
	pi.pi_min = 0;		// -1 is too non-intuitive to use w/ autopilot
	pi.pi_default = 0;
	pi.pi_max = 6;
	pi.pi_units = 1;
	pi.pi_ap_freq = AP_FREQ_MED;
	pi.pi_ap_rate = AP_RATE_MED;
	Params.speed = param_register("speed", &pi);

	param_key_register('-', KB_DEFAULT, Params.speed, -1);
	param_key_register('_', KB_DEFAULT, Params.speed, -1);
	param_key_register('+', KB_DEFAULT, Params.speed,  1);
	param_key_register('=', KB_DEFAULT, Params.speed,  1);
	param_key_register('6', KB_KEYPAD, Params.speed, -1);
	param_key_register('+', KB_KEYPAD, Params.speed,  1);

	/*
	 * Number of box blur passes to do.
	 */
	pi.pi_min = 1;
	pi.pi_default = 2;
	pi.pi_max = 3;
	pi.pi_units = 1;
	pi.pi_ap_freq = AP_FREQ_LOW;
	pi.pi_ap_rate = AP_RATE_LOW;
	Params.nbox = param_register("nbox", &pi);

	param_key_register('b', KB_DEFAULT, Params.nbox, -1);
	param_key_register('B', KB_DEFAULT, Params.nbox,  1);

	/*
	 * Which array of adjustments to use.
	 */
	pi.pi_min = 0;
	pi.pi_default = 0;
	pi.pi_max = (NADJTYPE - 1 + pi.pi_min);
	pi.pi_units = 1;
	pi.pi_ap_freq = AP_FREQ_LOW;
	pi.pi_ap_rate = AP_RATE_HIGH;
	Params.adjtype = param_register("adjtype", &pi);

	param_key_register('j', KB_DEFAULT, Params.adjtype, -1);
	param_key_register('J', KB_DEFAULT, Params.adjtype,  1);

	/*
	 * Which style of rendering to use.
	 */
	pi.pi_min = INT_MIN;
	pi.pi_default = 0;
	pi.pi_max = INT_MAX;
	pi.pi_units = 1;
	pi.pi_ap_freq = AP_FREQ_OFF;
	pi.pi_ap_rate = AP_RATE_OFF;
	Params.rendertype = param_register("rendertype", &pi);

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
