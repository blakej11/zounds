/*
 * types.h - some types used by several sources files.
 */

#ifndef	_TYPES_H
#define	_TYPES_H

#include <inttypes.h>
#include <sys/types.h>
#include <stdbool.h>

/*
 * For the various cl_* types.
 */
#if	defined(__APPLE__)
#include <OpenCL/opencl.h>
#elif	defined(__linux__)
#include <CL/opencl.h>
#else
#error	this platform is not supported
#endif

/*
 * Types shared between OpenCL and C.
 */
#include "vectypes.h"

/*
 * ------------------------------------------------------------------
 * Types specific to this program.
 */

/* A parameter ID. */
typedef unsigned int	param_id_t;

/* Which box kernel implementation to use. */
typedef enum {
	BK_MANUAL,
	BK_DIRECT,
	BK_SUBBLOCK,

	BK_NUM_KERNELS	// must be last
} box_kernel_t;

/* Which key bindings to use. */
typedef enum {
	KB_DEFAULT,	// default key bindings
	KB_KEYPAD,	// key bindings to use with "-K"

	KB_NUM_BINDINGS	// must come last
} key_binding_type_t;

#endif	/* _TYPES_H */
