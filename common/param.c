/*
 * param.c - handle tunable parameters, including "autopilot mode" which
 * tries to tune them in interesting ways.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <limits.h>
#include <sys/types.h>
#include <assert.h>

#include "common.h"

#include "debug.h"
#include "keyboard.h"
#include "module.h"
#include "param.h"
#include "randbj.h"
#include "util.h"

static void autopilot_disable(void);
static void autopilot_toggle(void);

/* ------------------------------------------------------------------ */

/*
 * Information about one parameter.
 */
typedef struct {
	const char	*name;		/* parameter name */
	int		min;		/* minimum value */
	int		max;		/* maximum value */
	float		units;		/* value * units = real value */

	int		ap_frequency;	/* frequency of autopiloting */
	int		ap_updaterate;	/* how fast AP should change it */

	int		value;		/* current parameter value */
	int		target;		/* AP's target parameter value */
} param_t;

/*
 * Callback for when a parameter changes.
 */
typedef struct {
	void		(*cb)(void);	/* function to call */
	uint64_t	ids;		/* param IDs to call it about */
} param_cb_t;

static struct {
	param_t		*value_table;	/* table of current parameters */
	int		value_count;
	int		value_nalloc;

	param_cb_t	*cb_table;	/* table of parameter callbacks */
	int		cb_count;
	int		cb_nalloc;

	param_id_t	noop;		/* for autopilot's sleep mode */
	bool		noop_registered;
} Param;

/* ------------------------------------------------------------------ */

static void
param_preinit(void)
{
	param_init_t    pi;

	debug_register_toggle('p', "parameters", DB_PARAM, NULL);

	/*
	 * This fake parameter is only used for its effect on autopilot.
	 * Namely, one of the operations that autopilot can take is to
	 * tweak this parameter, and it might spend a while trying to do
	 * that. That just looks like we're not changing any actual
	 * parameters, i.e. the image is just evolving as usual.
	 */
	bzero(&pi, sizeof (pi));
	pi.pi_ap_freq = AP_FREQ_HIGH;
	pi.pi_ap_rate = AP_RATE_PAUSE;
	Param.noop = param_register("no-op", &pi);
	Param.noop_registered = true;

	key_register('a', KB_DEFAULT, "toggle autopilot mode",
	    autopilot_toggle);

	/*
	 * I don't know why the Enter key on my keypad shows up as 0x03,
	 * but it does.
	 */
	key_register('\003', KB_KEYPAD, "toggle autopilot mode",
	    autopilot_toggle);
}

const module_ops_t	param_ops = {
	param_preinit,
	NULL,
	NULL
};

/* ------------------------------------------------------------------ */

/*
 * Register a tunable parameter.
 */
param_id_t
param_register(const char *name, param_init_t *pi)
{
	param_id_t	id;
	param_t		*param;

	/*
	 * Find a slot in the table, and grow the table if necessary.
	 */
	if (Param.value_count == Param.value_nalloc) {
		param_t		*oldtable = Param.value_table;
		const int	oldsize = Param.value_nalloc;

		if (oldsize == 0) {
			Param.value_nalloc = 8;
		} else {
			Param.value_nalloc = oldsize * 2;
		}

		Param.value_table =
		    calloc(Param.value_nalloc, sizeof (param_t));
		if (oldsize != 0) {
			memcpy(Param.value_table, oldtable,
			    oldsize * sizeof (param_t));
			mem_free((void **)&oldtable);
		}

	}
	assert(Param.value_count < Param.value_nalloc);
	id = Param.value_count;
	param = &Param.value_table[id];
	Param.value_count++;

	param->name = name;
	param->min = pi->pi_min;
	param->max = pi->pi_max;
	param->units = pi->pi_units;
	param->value = pi->pi_default;
	param->target = pi->pi_default;

	switch (pi->pi_ap_freq) {
	case AP_FREQ_OFF:		/* never use autopilot */
		param->ap_frequency = 0;
		break;
	case AP_FREQ_LOW:		/* don't tune often */
		param->ap_frequency = 1;
		break;
	case AP_FREQ_MED:
		param->ap_frequency = 4;
		break;
	case AP_FREQ_HIGH:		/* tune frequently */
		param->ap_frequency = 7;
		break;
	default:
		assert(0 && "bad pi_ap_freq");
	}

	switch (pi->pi_ap_rate) {
	case AP_RATE_OFF:		/* never use autopilot */
		param->ap_updaterate = INT_MAX;
		break;
	case AP_RATE_PAUSE:		/* tune very slowly */
		param->ap_updaterate = 100;
		break;
	case AP_RATE_LOW:
		param->ap_updaterate = 30;
		break;
	case AP_RATE_MED:
		param->ap_updaterate = 5;
		break;
	case AP_RATE_HIGH:		/* tune immediately */
		param->ap_updaterate = 0;
		break;
	default:
		assert(0 && "bad pi_ap_rate");
	}

	return (id);
}

/*
 * Register a function to be called when the parameter described by "id"
 * changes.
 */
void
param_cb_register(param_id_t id, void (*cb)())
{
	int	c;

	/*
	 * There is a separate table for callbacks, because a given
	 * callback may be called for more than one parameter.
	 */
	for (c = 0; c < Param.cb_count; c++) {
		if (Param.cb_table[c].cb == cb) {
			goto found;
		}
	}

	/*
	 * Find a slot in the table, and grow it if necessary.
	 */
	if (Param.cb_count == Param.cb_nalloc) {
		param_cb_t	*oldtable = Param.cb_table;
		const int	oldsize = Param.cb_nalloc;

		if (oldsize == 0) {
			Param.cb_nalloc = 8;
		} else {
			Param.cb_nalloc = oldsize * 2;
		}
		Param.cb_table =
		    calloc(Param.cb_nalloc, sizeof (param_cb_t));
		if (oldsize != 0) {
			memcpy(Param.cb_table, oldtable,
			    oldsize * sizeof (param_cb_t));
			mem_free((void **)&oldtable);
		}

	}
	assert(Param.cb_count < Param.cb_nalloc);
	c = Param.cb_count;
	Param.cb_count++;
	Param.cb_table[c].cb = cb;

found:
	/*
	 * If this is the first parameter ID we're associating with this
	 * callback, call the callback now to initialize it.
	 */
	if (Param.cb_table[c].ids == 0) {
		(*cb)();
	}

	assert(id < Param.value_count);
	assert(id != Param.noop || !Param.noop_registered);
	assert(id < 64);	/* cb_table[].ids is a uint64_t */
	assert(!(Param.cb_table[c].ids & (1ULL << id)));
	Param.cb_table[c].ids |= (1ULL << id);
}

void
param_cb_unregister(param_id_t id, void (*cb)())
{
	int	c;

	for (c = 0; c < Param.cb_count; c++) {
		if (Param.cb_table[c].cb == cb) {
			break;
		}
	}

	assert(c < Param.cb_count);
	assert(id < Param.value_count);
	assert(Param.cb_table[c].ids & (1ULL << id));
	Param.cb_table[c].ids &= ~(1ULL << id);
}

/*
 * Set the (autopilot) target value for the specified parameter.
 */
static bool
param_target_set(param_id_t id, int target)
{
	param_t		*param = &Param.value_table[id];
	const int	min = param->min;
	const int	max = param->max;

	assert(id < Param.value_count);
	assert(min <= target && target <= max);

	param->target = target;

	if (param->target != param->value) {
		debug(DB_PARAM, "Changing %s target: %d -> %d\n",
		    param->name, param->value, param->target);
	}

	return (param->target != param->value);
}

/*
 * Set a random target value for the specified parameter.
 */
static bool
param_target_randomize(param_id_t id)
{
	param_t		*param = &Param.value_table[id];
	const int	min = param->min;
	const int	max = param->max;

	return (param_target_set(id, (lrandbj() % (max - min + 1)) + min));
}

/*
 * Set the value of the specified parameter.
 */
static void
param_value_set(param_id_t id, int newvalue)
{
	param_t		*param = &Param.value_table[id];
	int		cb;

	if (id >= Param.value_count) {
		warn("param_value_set: no good: id %d vc %d\n",
		    id, Param.value_count);
	}
	assert(id < Param.value_count);
	if (param->min > newvalue || newvalue > param->max) {
		warn("param_value_set: no good: id %d min %d nv %d max %d\n",
		    id, param->min, newvalue, param->max);
	}
	assert(param->min <= newvalue && newvalue <= param->max);

	if (newvalue != param->value) {
		verbose(DB_PARAM, "Changing %s value: %d -> %d\n",
		    param->name, param->value, newvalue);

		param->value = newvalue;
	}

	/*
	 * Let anyone know who needs to know.
	 */
	for (cb = 0; cb < Param.cb_count; cb++) {
		if (Param.cb_table[cb].ids & (1ULL << id)) {
			(*Param.cb_table[cb].cb)();
		}
	}
}

/*
 * Adjust the value of the specified parameter up or down.
 */
static void
param_value_adjust(param_id_t id, int increment)
{
	param_t		*param = &Param.value_table[id];
	const int	min = param->min;
	const int	max = param->max;

	param_value_set(id, MAX(MIN(param->value + increment, max), min));
}

/*
 * Get the parameter's current value.
 */
int
param_int(param_id_t id)
{
	param_t	*const	param = &Param.value_table[id];

	assert(param->units == 1.0);
	return (param->value);
}

float
param_float(param_id_t id)
{
	param_t	*const	param = &Param.value_table[id];

	/*
	 * There's no actual reason why we couldn't have a float-valued
	 * parameter with units of 1. But generally if you specify units of 1,
	 * that means that you want it as an integer.
	 */
	assert(param->units != 1.0);
	return ((float)param->value * (float)param->units);
}

/*
 * Set the parameter's current value.
 */
void
param_set_int(param_id_t id, int newvalue)
{
	param_t	*const	param = &Param.value_table[id];

	assert(param->units == 1.0);

	param_value_set(id, newvalue);
	autopilot_disable();
}

void
param_set_float(param_id_t id, float newvalue)
{
	param_t	*const	param = &Param.value_table[id];
	const int	nv = (int)(newvalue / param->units);

	assert(param->units != 1.0);

	param_value_set(id, nv);
	autopilot_disable();
}

static void
param_key_cb(int id, int val)
{
	param_value_adjust((param_id_t)id, val);
	autopilot_disable();
}

/*
 * A wrapper around key_register*() to allow easy tweaking of parameters.
 *
 * When "key" is pressed, we will change the value of the parameter
 * described by "id" by the amount "val".
 */
void
param_key_register(unsigned char key, key_binding_type_t kb,
    param_id_t id, int val)
{
	param_t		*param = &Param.value_table[id];
	char		p[80];

	snprintf(p, 80, "%screase %s parameter by %d",
	    (val < 0 ? "de" : "in"), param->name, abs(val));

	key_register_args(key, kb, p, param_key_cb, (int)id, val);
}

/* ------------------------------------------------------------------ */

static struct {
	bool	enabled;
	int	steps;
	int	step_target;
} AP;

void
autopilot_enable(void)
{
	if (!AP.enabled) {
		verbose(DB_PARAM, "Autopilot enabled\n");
	}
	AP.enabled = true;

	AP.steps = 0;
	AP.step_target = 0;
}

static void
autopilot_disable(void)
{
	if (AP.enabled) {
		verbose(DB_PARAM, "Autopilot disabled\n");
	}
	AP.enabled = false;

	/* Force autopilot to pick new values next time it's enabled. */
	for (int id = 0; id < Param.value_count; id++) {
		param_t	*param = &Param.value_table[id];
		if (param->ap_frequency > 0) {
			param->target = param->value;
		}
	}
}

static void
autopilot_toggle(void)
{
	if (!AP.enabled) {
		autopilot_enable();
	} else {
		autopilot_disable();
	}
}

/*
 * Called before core_step(), so autopilot can provide fresh parameter values.
 */
void
param_step(void)
{
	int	id;

	if (!AP.enabled) {
		return;
	}

	/*
	 * If we're still pausing after the last operation, keep doing that.
	 */
	if (AP.steps++ < AP.step_target) {
		return;
	}
	AP.steps = 0;

	/*
	 * Are there any parameters that autopilot is still tweaking?
	 * If so, no need to choose a new one.
	 */
	for (id = 0; id < Param.value_count; id++) {
		param_t	*param = &Param.value_table[id];
		if (param->value != param->target && param->ap_frequency > 0) {
			break;
		}
	}

	if (id == Param.value_count) {	/* all parameters are at target */
		int		frequencies, r;

		/*
		 * Randomly choose a parameter to tune, based on the
		 * desired frequency for that parameter.
		 */
		frequencies = 0;
		for (id = 0; id < Param.value_count; id++) {
			frequencies += Param.value_table[id].ap_frequency;
		}
		if (frequencies == 0) {
			return;
		}

		r = lrandbj() % frequencies;
		for (id = 0; id < Param.value_count; id++) {
			const int	f = Param.value_table[id].ap_frequency;
			if (r < f) {
				break;
			}
			r -= f;
		}
		assert(id < Param.value_count);

		/*
		 * Got one. Pick a random target for it.
		 */
		param_target_randomize(id);

		param_t		*param = &Param.value_table[id];
		const int	rate = param->ap_updaterate;

		if (rate == 0) {	/* immediate */
			param_value_set(id, param->target);
		} else {
			/*
			 * See how long to wait before the next tweak.
			 */
			AP.step_target = lrandbj() % rate;

			debug(DB_PARAM, "Autopilot step target %d images%s\n",
			    AP.step_target,
			    (id == Param.noop) ? " (resting)" : "");
		}
	}

	/*
	 * Move parameters one step closer to their target.
	 */
	for (id = 0; id < Param.value_count; id++) {
		param_t		*param = &Param.value_table[id];

		if (param->ap_frequency == 0) {
			continue;
		} else if (param->value < param->target) {
			param_value_adjust(id, 1);
		} else if (param->value > param->target) {
			param_value_adjust(id, -1);
		}
	}
}
