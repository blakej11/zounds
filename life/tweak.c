/*
 * tweak.c - holds the policy parts of the Physarum algorithm.
 */

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <math.h>
#include <assert.h>
#include <limits.h>

#include "common.h"
#include "datasrc.h"
#include "debug.h"
#include "keyboard.h"
#include "window.h"
#include "param.h"
#include "tweak.h"

/* ------------------------------------------------------------------ */

/*
 * The tweakable parameters, and their default values.
 */
static const param_init_t Param_values[] = {
     /* min, def, max, units,    freq,    rate, abbr, name */
	{ 1,  75, 100, 0.01f, APF_OFF, APR_LOW, "AL", "aliveness" }
	// Aliveness threshold.
};

/* ------------------------------------------------------------------ */

/*
 * The parameter IDs, used for calling into the param subsystem.
 */
static struct {
	param_id_t	aliveness;
} Params;

void
tweak_preinit(void)
{
	param_register_table(Param_values,
	    sizeof (Param_values) / sizeof (*Param_values));

	Params.aliveness = param_lookup("aliveness");
	param_key_register('-', KB_DEFAULT, Params.aliveness, -1);
	param_key_register('_', KB_DEFAULT, Params.aliveness, -1);
	param_key_register('+', KB_DEFAULT, Params.aliveness,  1);
	param_key_register('=', KB_DEFAULT, Params.aliveness,  1);

}

/* ------------------------------------------------------------------ */

float
tweak_aliveness(void)
{
	return (param_float(Params.aliveness));
}
