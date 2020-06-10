/*
 * XXX don't have a way of vectoring direct keypad input over here
 */

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
#include "osdep.h"
#include "param.h"
#include "randbj.h"
#include "util.h"

static void autopilot_disable(void);
static void autopilot_toggle(void);
static const char *param_dump_defaultstr(void);

/* ------------------------------------------------------------------ */

/*
 * Information about one parameter.
 */
typedef struct {
	param_init_t	pi;		/* initial values */

	int		value;		/* current parameter value */

	/* Data used by autopilot: */
	int		ap_target;	/* target parameter value */
	int		ap_frequency;	/* frequency of autopiloting */
	hrtime_t	ap_delay;	/* delay between each tweak */
	hrtime_t	ap_nextstep;	/* time of next tweak */
} param_t;

/*
 * Callback for when a parameter changes.
 */
typedef struct {
	void		(*cb)(void);	/* function to call */
	uint64_t	ids;		/* param IDs to call it about */
} param_cb_t;

/*
 * Data for this subsystem.
 */
static struct {
	param_t		*value_table;	/* table of current parameters */
	int		value_count;
	int		value_nalloc;

	param_cb_t	*cb_table;	/* table of parameter callbacks */
	int		cb_count;
	int		cb_nalloc;

	param_preset_t	*preset_table;	/* table of parameter presets */
	int		preset_count;
	int		preset_nalloc;

	int		preset_frequency; /* how often AP should use presets */
	bool		preset_going;	/* are we using a preset? */

	param_id_t	noop;		/* for autopilot's sleep mode */
	bool		noop_registered;

	bool		ap_enabled;	/* is autopilot enabled? */
} Param;

/* ------------------------------------------------------------------ */

static void
key_dump_params(void)
{
	char	str[80];

	param_dump(str, sizeof (str) - 1);
	note("Param string: %s\n", str);
}

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
	pi.pi_ap_freq = APF_HIGH;
	pi.pi_ap_rate = APR_PAUSE;
	pi.pi_name = "no-op";
	Param.noop = param_register(&pi);
	Param.noop_registered = true;

	key_register('P', KB_DEFAULT, "dump param string", key_dump_params);

	key_register('a', KB_DEFAULT, "toggle autopilot mode",
	    autopilot_toggle);

	/*
	 * I don't know why the Enter key on my keypad shows up as 0x03,
	 * but it does.
	 */
	key_register('\003', KB_KEYPAD, "toggle autopilot mode",
	    autopilot_toggle);

	/*
	 * Add a preset that gets us back to the defaults.
	 */
	param_preset_t	pp;

	pp.pp_dumpstr = param_dump_defaultstr();
	pp.pp_descr = "default settings";
	param_register_preset_table(&pp, 1);

	Param.preset_going = false;
}

const module_ops_t	param_ops = {
	param_preinit,
	NULL,
	NULL
};

/*
 * ------------------------------------------------------------------
 * Getting and setting parameter values and targets.
 */

typedef enum {
	PU_VALUE,
	PU_TARGET
} param_update_t;

/*
 * Set the specified parameter's value or autopilot target.
 */
static void
param_set(param_id_t id, param_update_t pu, int nv)
{
	param_t		*param = &Param.value_table[id];
	const int	min = param->pi.pi_min;
	const int	max = param->pi.pi_max;
	int		cb;
	int		*ovp;
	const char	*name;

	if (id >= Param.value_count) {
		warn("param_set: no good: id %d vc %d\n",
		    id, Param.value_count);
	}
	assert(id < Param.value_count);
	if (min > nv || nv > max) {
		warn("param_set: no good: id %d min %d nv %d max %d\n",
		    id, min, nv, max);
	}
	assert(min <= nv && nv <= max);
	assert(pu == PU_VALUE || pu == PU_TARGET);

	if (pu == PU_VALUE) {
		ovp = &param->value;
		name = "value";
	} else {
		ovp = &param->ap_target;
		name = "target";
	}

	if (nv != *ovp) {
		verbose(DB_PARAM, "Changing %s %s: %d -> %d\n",
		    param->pi.pi_name, name, *ovp, nv);
		*ovp = nv;
	}

	/*
	 * If the value is updated, let anyone know who needs to know.
	 */
	if (pu == PU_VALUE) {
		for (cb = 0; cb < Param.cb_count; cb++) {
			if (Param.cb_table[cb].ids & (1ULL << id)) {
				(*Param.cb_table[cb].cb)();
			}
		}
	}
}

static void
param_target_set(param_id_t id, int nt)
{
	param_set(id, PU_TARGET, nt);
}

/*
 * Pick a random autopilot target value for the specified parameter.
 */
static int
param_choose_target(param_id_t id)
{
	param_t		*param = &Param.value_table[id];
	const int	min = param->pi.pi_min;
	const int	max = param->pi.pi_max;
	const int	nv = (lrandbj() % (max - min + 1)) + min;

	return (nv);
}

static void
param_value_set(param_id_t id, int nv)
{
	param_set(id, PU_VALUE, nv);
}

/*
 * Adjust the value of the specified parameter up or down.
 */
static void
param_value_adjust(param_id_t id, int increment)
{
	param_t		*param = &Param.value_table[id];
	const int	min = param->pi.pi_min;
	const int	max = param->pi.pi_max;
	const int	nv = MAX(MIN(param->value + increment, max), min);

	param_value_set(id, nv);
}

/*
 * Get a parameter's current value.
 */
int
param_int(param_id_t id)
{
	param_t	*const	param = &Param.value_table[id];

	assert(param->pi.pi_units == 1.0);
	return (param->value);
}

float
param_float(param_id_t id)
{
	param_t	*const	param = &Param.value_table[id];

	return ((float)param->value * (float)param->pi.pi_units);
}

/*
 * Set the parameter's current value.
 */
void
param_set_int(param_id_t id, int newvalue)
{
	param_t	*const	param = &Param.value_table[id];

	assert(param->pi.pi_units == 1.0);

	param_value_set(id, newvalue);
	autopilot_disable();
}

void
param_set_float(param_id_t id, float newvalue)
{
	param_t	*const	param = &Param.value_table[id];
	const int	nv = (int)(newvalue / param->pi.pi_units);

	assert(param->pi.pi_units != 1.0);

	param_value_set(id, nv);
	autopilot_disable();
}

static void
param_reset_to_defaults_withcb(void (*cb)(param_id_t, int))
{
	for (param_id_t id = 0; id < Param.value_count; id++) {
		param_t	*const	param = &Param.value_table[id];
		cb(id, param->pi.pi_default);
	}
}

/*
 * Reset all parameters to their default values.
 */
void
param_reset_to_defaults(void)
{
	param_reset_to_defaults_withcb(param_value_set);
}

/*
 * ------------------------------------------------------------------
 * Registering and unregistering parameters, presets, and callbacks.
 */

static int
ap_frequency(ap_freq_t apf)
{
	switch (apf) {
	case APF_OFF:		/* never use autopilot */
		return (0);
	case APF_LOW:		/* don't tune often */
		return (1);
	case APF_MED:
		return (4);
	case APF_HIGH:		/* tune frequently */
		return (7);
	default:
		die("ap_frequency: invalid ap_freq_t \"%d\"\n", apf);
		return (0);	/*NOTREACHED*/
	}
}

/*
 * Given an ap_rate_t, return a number of nanoseconds to delay between
 * each update.
 */
static hrtime_t
ap_pick_delay(ap_rate_t apr)
{
	double	delay_sec;

	switch (apr) {
	case APR_PAUSE:		/* how long to pause if not tuning anything */
		delay_sec = 7.0f;
		break;
	case APR_LOW:
		delay_sec = 1.5f;
		break;
	case APR_MED:
		delay_sec = 0.3f;
		break;
	case APR_HIGH:		/* tune immediately */
		/*
		 * "0" isn't even fast enough - that would just mean "update
		 * the value by +1/-1 every frame", rather than "update the
		 * value all the way to the target immediately".
		 *
		 * autopilot_target() does this already, so it shouldn't
		 * be calling in here.
		 */
		die("ap_pick_delay: should not be called with APR_HIGH\n");
		delay_sec = 0.0f;	/*NOTREACHED*/
	default:
		die("ap_pick_delay: invalid ap_rate_t \"%d\"\n", apr);
		delay_sec = 0.0f;	/*NOTREACHED*/
	}

	/* Spread it out using a normal distribution. */
	delay_sec *= (1.0 + normrandbj() / 4.0);

	if (delay_sec < 0) {
		return (0);
	} else {
		return ((hrtime_t)(delay_sec * 1000000000.0));
	}
}

/*
 * Register a tunable parameter.
 */
param_id_t
param_register(const param_init_t *pi)
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

	param->pi = *pi;		/* structure copy */
	if (pi->pi_abbrev != NULL) {
		param->pi.pi_abbrev = strdup(pi->pi_abbrev);
	} else {
		param->pi.pi_abbrev = "";
	}
	if (pi->pi_name != NULL) {
		param->pi.pi_name = strdup(pi->pi_name);
	} else {
		param->pi.pi_name = "";
	}
	param->value = pi->pi_default;
	param->ap_target = pi->pi_default;
	param->ap_frequency = ap_frequency(pi->pi_ap_freq);
	param->ap_nextstep = 0;
	param->ap_delay = 0;

	return (id);
}

/*
 * Register an array of tunable parameters.
 */
void
param_register_table(const param_init_t *pi, size_t nparam)
{
	for (size_t i = 0; i < nparam; i++) {
		(void) param_register(&pi[i]);
	}
}

/*
 * Register a table of interesting preset parameter values.
 */
void
param_register_preset_table(const param_preset_t *pp, size_t npreset)
{
	assert(npreset > 0);

	/*
	 * Grow the table if necessary.
	 */
	if (Param.preset_count + npreset > Param.preset_nalloc) {
		param_preset_t	*oldtable = Param.preset_table;
		const int	oldsize = Param.preset_nalloc;

		Param.preset_nalloc = Param.preset_count + npreset;
		Param.preset_table =
		    calloc(Param.preset_nalloc, sizeof (param_preset_t));
		if (oldsize != 0) {
			memcpy(Param.preset_table, oldtable,
			    oldsize * sizeof (param_preset_t));
			mem_free((void **)&oldtable);
		}

	}

	for (size_t i = 0; i < npreset; i++) {
		param_preset_t	*const	tpp =
		    &Param.preset_table[Param.preset_count];
		const param_preset_t	*app = &pp[i];	/* argument pp */

		assert(Param.preset_count < Param.preset_nalloc);
		assert(tpp->pp_dumpstr == NULL);
		assert(tpp->pp_descr == NULL);

		tpp->pp_dumpstr = strdup(app->pp_dumpstr);
		tpp->pp_descr = strdup(app->pp_descr);

		Param.preset_count++;
	}

	Param.preset_frequency = MAX(10, 50 / Param.preset_count);
}

/*
 * Useful for getting the ID of a parameter registered via
 * param_register_table().
 */
param_id_t
param_lookup(const char *name)
{
	for (param_id_t id = 0; id < Param.value_count; id++) {
		param_t	*const	param = &Param.value_table[id];

		if (strcmp(name, param->pi.pi_name) == 0) {
			return (id);
		}
	}

	die("param_lookup: failed to find parameter \"%s\"\n", name);
	return (0); /*NOTREACHED*/
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
		Param.cb_table = calloc(Param.cb_nalloc, sizeof (param_cb_t));
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
	char		comment[80];

	snprintf(comment, 80, "%screase %s parameter by %d",
	    (val < 0 ? "de" : "in"), param->pi.pi_name, abs(val));

	key_register_param(key, kb, comment, param_key_cb, (int)id, val);
}

/*
 * ------------------------------------------------------------------
 * Dumping and restoring the current parameter values.
 */

/*
 * Change this string if the parameter parsing logic changes incompatibly.
 */
#define	PARAM_DUMP_MAGIC	"a"

/*
 * A string that will cause param_undump() to restore default param settings.
 */
static const char *
param_dump_defaultstr(void)
{
	return (PARAM_DUMP_MAGIC);
}

static void
param_dump_append(char **bufp, size_t *lenp, const char *str, int val)
{
	const int	nchar = snprintf(NULL, 0, "%s%d", str, val);

	if (*lenp - nchar >= 1) {
		char	*const	buf = *bufp;
		size_t		len = *lenp;

		snprintf(buf, len, "%s%d", str, val);
		*bufp = buf + nchar;
		*lenp = len - nchar;
	}
}

void
param_dump(char *buf, size_t len)
{
	if (len <= sizeof (PARAM_DUMP_MAGIC)) {
		return;
	}
	for (char *p = PARAM_DUMP_MAGIC; *p != '\0'; len--) {
		*buf++ = *p++;
	}

	for (param_id_t id = 0; id < Param.value_count; id++) {
		param_t	*const	param = &Param.value_table[id];
		const char	*abbrev = param->pi.pi_abbrev;
		const int	val = Param.value_table[id].value;

		if (strlen(abbrev) > 0 && val != param->pi.pi_default) {
			param_dump_append(&buf, &len, abbrev, val);
		}
	}

	*buf = '\0';
}

static void
param_undump_withcb(const char *buf, void (*cb)(param_id_t id, int val))
{
	if (strncmp(buf, PARAM_DUMP_MAGIC, strlen(PARAM_DUMP_MAGIC)) != 0) {
		warn("param_undump: invalid dump string \"%s\"\n", buf);
		return;
	}
	buf += strlen(PARAM_DUMP_MAGIC);

	param_reset_to_defaults_withcb(cb);

	for (;;) {
		char		a[3];
		int		val;
		param_id_t	id;

		if (strlen(buf) < 2) {
			return;
		}
		strncpy(a, buf, 2);
		a[2] = '\0';

		for (buf += 2, val = 0; *buf >= '0' && *buf <= '9'; buf++) {
			val = val * 10 + (*buf - '0');
		}

		for (id = 0; id < Param.value_count; id++) {
			param_t	*const	param = &Param.value_table[id];
			const char	*abbrev = param->pi.pi_abbrev;

			if (strcmp(a, abbrev) == 0) {
				(*cb)(id, val);
				break;
			}
		}
		if (id == Param.value_count) {
			warn("param_undump: "
			    "failed to recognize param \"%s\"\n", a);
		}
	}
}

void
param_undump(const char *buf)
{
	param_undump_withcb(buf, param_value_set);
}

/*
 * ------------------------------------------------------------------
 * The parameter autopilot.
 */

void
autopilot_enable(void)
{
	if (!Param.ap_enabled) {
		verbose(DB_PARAM, "Autopilot enabled\n");
	}
	Param.ap_enabled = true;

	for (param_id_t id = 0; id < Param.value_count; id++) {
		param_t	*param = &Param.value_table[id];

		param->ap_target = param->value;
		param->ap_nextstep = 0;
		param->ap_delay = 0;
	}
}

static void
autopilot_disable(void)
{
	if (Param.ap_enabled) {
		verbose(DB_PARAM, "Autopilot disabled\n");
	}
	Param.ap_enabled = false;
}

static void
autopilot_toggle(void)
{
	if (!Param.ap_enabled) {
		autopilot_enable();
	} else {
		autopilot_disable();
	}
}

/*
 * Set a parameter's target value.
 */
static void
autopilot_target(param_id_t id, int target)
{
	param_t		*param = &Param.value_table[id];
	const ap_rate_t	rate = param->pi.pi_ap_rate;

	if (rate == APR_HIGH) {			/* immediate */
		param_target_set(id, target);
		param_value_set(id, target);
	} else if (param->value != target || id == Param.noop) {
		/*
		 * Don't take more than 30 seconds to cycle a given parameter.
		 */
		const int	delta = MAX(1, abs(param->value - target));
		const hrtime_t	maxdelay = 30000 / delta;
		const hrtime_t	delay_msec = 
		    MIN(maxdelay, ap_pick_delay(rate) / 1000000);
		const hrtime_t	delay = delay_msec * 1000000;

		/*
		 * See how long to wait before the next tweak.
		 */
		param->ap_delay = delay;
		param->ap_nextstep = gethrtime() + param->ap_delay;

		if (id == Param.noop) {
			debug(DB_PARAM,
			    "Autopilot: resting for %d.%03d sec\n",
			    delay_msec / 1000, delay_msec % 1000);
		} else {
			debug(DB_PARAM, "Autopilot: stepping %s "
			    "from %d to %d once every %d.%03d sec\n",
			    param->pi.pi_name, param->value, target,
			    delay_msec / 1000, delay_msec % 1000);
		}
		param_target_set(id, target);
	}
}

/*
 * A pause is implemented by setting the target on the "no-op" parameter.
 * The value of the target doesn't actually change, but this sets its
 * ap_nextstep as a side effect.
 */
static void
autopilot_pause(void)
{
	const param_id_t	id = Param.noop;
	param_t		*const	param = &Param.value_table[id];
	int			target = param->ap_target;

	autopilot_target(id, target);
}

/*
 * Called before core_step(), so autopilot can provide fresh parameter values.
 * This runs the parameter autopilot.
 */
void
autopilot_step(void)
{
	param_id_t	id;
	hrtime_t	now;

	if (!Param.ap_enabled) {
		return;
	}

	/*
	 * Are there any parameters that autopilot is still tweaking?
	 * If so, no need to choose a new one.
	 */
	now = gethrtime();
	for (id = 0; id < Param.value_count; id++) {
		param_t	*param = &Param.value_table[id];
		if (param->ap_target != param->value ||
		    param->ap_nextstep > now) {
			break;
		}
	}

	if (id == Param.value_count) {	/* all parameters are at target */

		if (Param.preset_going) {
			/* just finished a preset string; delay a bit */

			Param.preset_going = false;
			autopilot_pause();

		} else if ((lrandbj() % (Param.preset_frequency + 1)) == 0) {
			/* use a preset string - maybe just the defaults */

			const size_t	i = lrandbj() % Param.preset_count;
			param_preset_t	*pp = &Param.preset_table[i];

			debug(DB_PARAM,
			    "Autopilot: using \"%s\" preset string\n",
			    pp->pp_descr);

			param_undump_withcb(pp->pp_dumpstr, autopilot_target);
			debug(DB_PARAM, "Autopilot: finished preset string\n");
			Param.preset_going = true;
		} else {
			/* tweak one parameter */

			int		frequencies, r, ntarg;

			/*
			 * Randomly choose a parameter to tune, based on the
			 * desired frequency for that parameter.
			 */
			frequencies = 0;
			for (id = 0; id < Param.value_count; id++) {
				frequencies +=
				    Param.value_table[id].ap_frequency;
			}
			if (frequencies == 0) {
				return;
			}

			r = lrandbj() % frequencies;

			for (id = 0; id < Param.value_count; id++) {
				const int	f =
				    Param.value_table[id].ap_frequency;
				if (r < f) {
					break;
				}
				r -= f;
			}
			assert(id < Param.value_count);

			/*
			 * Got one. Choose and set a random target for it.
			 */
			autopilot_target(id, param_choose_target(id));
		}
	}

	/*
	 * Move parameters one step closer to their target.
	 */
	for (id = 0; id < Param.value_count; id++) {
		param_t		*param = &Param.value_table[id];

		if (param->ap_nextstep > now) {
			continue;
		} else if (param->value < param->ap_target) {
			param->ap_nextstep = now + param->ap_delay;
			param_value_adjust(id, 1);
		} else if (param->value > param->ap_target) {
			param->ap_nextstep = now + param->ap_delay;
			param_value_adjust(id, -1);
		}
	}
}
