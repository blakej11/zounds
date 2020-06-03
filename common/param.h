/*
 * param.h - interfaces for setting, tuning, and auto-tuning parameters.
 */

#ifndef	_PARAM_H
#define	_PARAM_H

#include "types.h"

/*
 * How frequently autopilot should tune this parameter.
 */
typedef enum {
	APF_OFF = 0,			// do not use autopilot
	APF_LOW,			// don't tune often
	APF_MED,			// 
	APF_HIGH			// tune frequently
} ap_freq_t;

/*
 * How fast-moving should autopilot's updates be.
 *
 * Even though there is an APF_OFF, there is no APR_OFF. Giving the autopilot
 * access to tables of preset parameters means that it can wind up tweaking
 * any parameter that could show up in a param_dump() string. So even if a
 * parameter shouldn't be part of the default set of autopilot tweaks, it
 * should have a tweak rate specified.
 */
typedef enum {
	APR_PAUSE = 0,			// change value very slowly
	APR_LOW,			// change value slowly
	APR_MED,			//
	APR_HIGH			// change value immediately
} ap_rate_t;

/*
 * A structure for registering a tunable parameter.
 *
 * Notice that the min/max/default values are all integers. In some cases
 * the meaning of the parameter is naturally integer-valued, and this is
 * exactly what's desired. In that case, pi_units should have the value 1.
 *
 * If the parameter is not naturally integer-valued, pi_units should be set to 
 * the basic unit of increment/decrement for this parameter. For example, a
 * parameter that corresponded to one of sixteen compass points on a circle
 * would have pi_units = M_PI / 8 (i.e. 2*pi radians / 16 points).
 */
typedef struct {
	int		pi_min;		// minimum value
	int		pi_default;	// default value
	int		pi_max;		// maximum value
	float		pi_units;	// multiply by this when returning value
	ap_freq_t	pi_ap_freq;	// see above
	ap_rate_t	pi_ap_rate;	// see above
	const char	*pi_abbrev;	// two-character abbreviation of name
	const char	*pi_name;	// full name of parameter
} param_init_t;

/*
 * A structure for tracking sets of interesting parameter values,
 * as dumped out via param_dump().
 */
typedef struct {
	const char	*pp_dumpstr;	// dumped parameter string
	const char	*pp_descr;	// human-readable description
} param_preset_t;

/* ------------------------------------------------------------------ */

/*
 * Register a new tunable parameter.  "pi" points to a filled-in param_init_t
 * describing its values.
 */
extern param_id_t
param_register(const param_init_t *pi);

/*
 * Register a table of tunable parameters.
 */
extern void
param_register_table(const param_init_t *pi, size_t nparam);

/*
 * Register a table of interesting preset parameter values.
 */
extern void
param_register_preset_table(const param_preset_t *pp, size_t npreset);

/*
 * Look up a parameter by its name. This aborts if an invalid name is passed.
 */
extern param_id_t
param_lookup(const char *name);

/*
 * Register callback "cb" to be called whenever the parameter with id "id"
 * changes. The callback is called immediately if this is the first parameter
 * it is being associated with.
 */
extern void
param_cb_register(param_id_t id, void (*cb)());

/*
 * Remove the registration for callback "cb" with parameter "id".
 */
extern void
param_cb_unregister(param_id_t id, void (*cb)());

/*
 * When key "key" is pressed when we're using keyboard type "kb", change the
 * value of parameter "id" by "val".  (See the definition of param_init_t
 * for how "val" is interpreted.)
 */
extern void
param_key_register(unsigned char key, key_binding_type_t kb,
    param_id_t id, int val);

/*
 * Get the current actual value of parameter "id".  This must be a parameter
 * with integer values, i.e. it was created with pi_units = 1.
 */
extern int
param_int(param_id_t id);

/*
 * Get the current actual value of parameter "id".  This must be a parameter
 * with non-integer values, i.e. it was created with pi_units != 1.
 */
extern float
param_float(param_id_t id);

/*
 * Set the current actual value of parameter "id".  This must be a parameter
 * with integer values, i.e. it was created with pi_units = 1.
 */
extern void
param_set_int(param_id_t id, int val);

/*
 * Set the current actual value of parameter "id".  This must be a parameter
 * with non-integer values, i.e. it was created with pi_units != 1.
 */
extern void
param_set_float(param_id_t id, float val);

/*
 * Reset all parameters to their default values.
 */
extern void
param_reset_to_defaults(void);

/*
 * Dump the current parameters into a string.
 */
extern void
param_dump(char *str, size_t len);

/*
 * Set the parameters based on a dumped string.
 */
extern void
param_undump(const char *str);

/* ------------------------------------------------------------------ */

extern void
autopilot_step(void);

extern void
autopilot_enable(void);

#endif	/* _PARAM_H */
