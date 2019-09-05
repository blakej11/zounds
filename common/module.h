/*
 * module.h - common interface for simplifying initialization and cleanup.
 */

#ifndef	_MODULE_H
#define	_MODULE_H

#include "types.h"

/*
 * This structure describes how each subsystem initializes and cleans up.
 * A subsystem can fill in as few or as many of these callbacks as it needs.
 *
 * All preinit callbacks are called before any init callbacks are called,
 * as with the fini vs. postfini callbacks.  But the callbacks of a specific
 * type may be invoked in any order.
 */
typedef struct {
	/*
	 * This is only called once, at the very start of the program.
	 * It can register callbacks (key_register*(), debug_register*(),
	 * param_register*(), and glut*Func()), call some specifically
	 * selected functions (interp_enable()), and initialize global data,
	 * but it should not make arbitrary calls into other subsystems.
	 */
	void	(*preinit)(void);

	/*
	 * Called at the start of the program, as well as any time we resize.
	 * Any data that depends on the value of "Width" or "Height" must
	 * be allocated here, and not in the preinit() callback. OpenCL can
	 * be used in this callback.
	 */
	void	(*init)(void);

	/*
	 * Called at the end of the program, as well as any time we resize.
	 * Any data that depends on the value of "Width" or "Height" must
	 * be freed here, and any OpenCL resources must be freed here.
	 */
	void	(*fini)(void);

	/*
	 * This is only called at the very end of the program.  Most
	 * subsystems don't need this.
	 */
	void	(*postfini)(void);
} module_ops_t;

extern void	module_preinit(void);
extern void	module_init(void);
extern void	module_fini(void);
extern void	module_postfini(void);

#endif	/* _MODULE_H */
