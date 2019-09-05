/*
 * debug.h - interfaces for debugging and investigation.
 */

#ifndef	_DEBUG_H
#define	_DEBUG_H

#include "types.h"
#include "common.h"

/*
 * These must all have distinct bit values, as they're used as a bitmap.
 */
typedef enum {
	DB_BOX		= 0x00000001,	/* box blur */
	DB_CORE		= 0x00000002,	/* core algorithm */
	DB_DEBUG	= 0x00000004,	/* debugging code */
	DB_HEAT		= 0x00000008,	/* heatmap */
	DB_HISTO	= 0x00000010,	/* text histogram */
	DB_IMAGE	= 0x00000020,	/* image I/O */
	DB_INTERP	= 0x00000040,	/* interpolation */
	DB_MOUSE	= 0x00000080,	/* mouse */
	DB_OPENCL	= 0x00000100,	/* OpenCL wrappers */
	DB_PARAM	= 0x00000200,	/* parameter tracking */
	DB_PERF		= 0x00000400,	/* performance */
	DB_SKIP		= 0x00000800,	/* image skipping */
	DB_STROKE	= 0x00001000,	/* stroke processing */
	DB_WINDOW	= 0x00002000,	/* window handling */
} debug_area_t;

/*
 * General-purpose text output routines.
 *
 * - debug() is printed only if the specified area has been activated.
 * - verbose() is printed in "verbose" mode or if the specified area is active.
 * - note() is always printed.  (Just like printf(), but returns void.)
 * - warn() is used for non-fatal error conditions.
 * - die() is used for fatal error conditions; it causes the program to exit.
 * - ocl_die() is used for OpenCL errors.
 *
 * warn() and die() append details about the current "errno" if the format
 * string does not include a newline.  ocl_die() appends details about the
 * OpenCL error described by "err".
 */
extern void
debug(debug_area_t area, const char *fmt, ...);

extern void
verbose(debug_area_t area, const char *fmt, ...);

extern void
note(const char *fmt, ...);

extern void
warn(const char *fmt, ...);

extern void
die(const char *fmt, ...);

extern void
ocl_die(int err, const char *fmt, ...);

/*
 * It's sometimes useful to trace a single pixel's data through multiple
 * stages of processing.  debug_offset() returns a consistent offset
 * (i.e. a value of Y * W + X) of an arbitrarily chosen single pixel that all
 * subsystems can use.
 */
extern pix_t
debug_offset(void);

/*
 * Register a subsystem's debug processing, as described at the top of debug.c.
 *
 * - "key" is the keypress to toggle that subsystem
 * - "comment" is a very brief description of that subsystem
 * - "area" is a debug_area_t value (or 0, to just have the callback called)
 * - "cb" is a callback which is called after toggling that area
 */
extern void
debug_register_toggle(unsigned char key,
    const char *comment, debug_area_t area, void (*cb)());

/*
 * Returns true if debugging for the specified area has been activated.
 */
extern bool
debug_enabled(debug_area_t);

/*
 * Toggles debugging for the subsystem described by "key".
 */
extern void
debug_toggle(unsigned char key);

/* ------------------------------------------------------------------ */

extern void
debug_set_verbose(void);

extern void
debug_init_areas(const char *);

#endif	/* _DEBUG_H */
