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
	AP_FREQ_OFF	= 0,		// do not use autopilot
	AP_FREQ_LOW,			// don't tune often
	AP_FREQ_MED,			// 
	AP_FREQ_HIGH			// tune frequently
} ap_freq_t;

/*
 * How fast-moving should autopilot's updates be.
 */
typedef enum {
	AP_RATE_OFF	= 0,		// do not use autopilot
	AP_RATE_PAUSE,			// change value very slowly
	AP_RATE_LOW,			// change value slowly
	AP_RATE_MED,			//
	AP_RATE_HIGH			// change value immediately
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
	int		pi_max;		// maximum value
	int		pi_default;	// default value
	float		pi_units;	// multiply by this when returning value
	ap_freq_t	pi_ap_freq;	// see above
	ap_rate_t	pi_ap_rate;	// see above
} param_init_t;

/*
 * Register a new tunable parameter.  "name" is the name of the parameter,
 * and "pi" points to a filled-in param_init_t describing its values.
 */
extern param_id_t
param_register(const char *name, param_init_t *pi);

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

/* ------------------------------------------------------------------ */

extern void
param_step(void);

extern void
autopilot_enable(void);

#endif	/* _PARAM_H */
