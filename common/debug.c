/*
 * debug.c - code to allow fine-grained control over what messages are printed.
 *
 * A subsystem can define a member of the debug_area_t enumeration in debug.h.
 * To enable that enumeration, it must call debug_register_toggle() before
 * using it.  Debugging messages from that subsystem can then be displayed by:
 *
 * - Using the keyboard: the key "D" is an indicator that the next key is
 *   used to choose a subsystem to enable/disable.  So, for example, typing
 *   "D x" will toggle the subsystem that passed 'x' to debug_register_toggle().
 *   If that subsystem had previously been enabled, this will disable it.
 *
 * - Using the command line: if a "-D" option is passed, then the next
 *   argument is treated as a list of subsystems to enable.  So, for example,
 *   passing "-D xyz" is equivalent to typing "D x D y D z".
 *
 * The special debug character "*" means "enable/disable all subsystems".
 * This can be done either by typing "D *" or by passing "-D \*" on the
 * command line.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <assert.h>
#include <stdarg.h>
#include <errno.h>

#include "common.h"

#include "debug.h"
#include "module.h"
#include "keyboard.h"
#include "util.h"

static void	key_v(void);
static void	debug_help(void);
static void	debug_toggle_all(void);

/* ------------------------------------------------------------------ */

typedef struct {
	const char	*comment;	/* Descriptive name */
	debug_area_t	area;		/* Which area of code uses it */
	void		(*cb)(void);	/* Callback, if desired */
} debug_toggle_t;

static struct {
	bool		verbose;	/* Print important updates */
	debug_area_t	areas;		/* Which areas to print details */

	debug_area_t	allareas;	/* All areas that we know of */
	bool		alldebug;	/* For toggling all areas at once */
	bool		initialized;	/* Far enough along that we can print */

	char		*initstr;	/* "-D" command line arg */
	int		ninit;		/* number of areas in initstr */

	/*
	 * A convenient place to keep track of a single offset within
	 * the image to look at, if we want to track multiple steps
	 * of processing.
	 */
	int		offset;

	debug_toggle_t	toggles[UCHAR_MAX + 1];		/* The key map */
} Debug;

/* ------------------------------------------------------------------ */

/*
 * Called from main(), before preinit(), to initialize verbosity.
 */
void
debug_set_verbose(void)
{
	Debug.verbose = true;
}

/*
 * Called from main(), before preinit(), to initialize debug levels.
 */
void
debug_init_areas(const char *str)
{
	Debug.ninit = strlen(str);
	Debug.initstr = mem_alloc(Debug.ninit);
	strcpy(Debug.initstr, str);
}

static void
debug_preinit(void)
{
	key_register('v', KB_DEFAULT, "toggle verbose mode", key_v);

	debug_register_toggle('d', "debug", DB_DEBUG, NULL);
	debug_register_toggle('*', "all areas", 0, debug_toggle_all);
	debug_register_toggle('?', "<this help>", 0, debug_help);

	Debug.offset = 0;
}

static void
debug_init(void)
{
	int	i;

	assert((Debug.ninit == 0) == (Debug.initstr == NULL));

	if (Debug.ninit == 0) {
		return;
	}

	/*
	 * If we get here, that means we've done all of the preinit callbacks,
	 * and at least one unrecognized debugging area was passed as part
	 * of the "-D" command line option.  This could either be a command
	 * line typo, or a bug in a subsystem.
	 */
	assert(strlen(Debug.initstr) == Debug.ninit);
	debug(DB_DEBUG, "Unknown debugging areas");
	for (i = 0; i < Debug.ninit; i++) {
		debug(DB_DEBUG, "%s \"%c\"",
		    (i > 0 ? "," : ""), Debug.initstr[i]);
	}
	debug(DB_DEBUG, "\n");
}

const module_ops_t	debug_ops = {
	debug_preinit,
	debug_init
};

/* ------------------------------------------------------------------ */

static void
key_v(void)
{
	Debug.verbose = !Debug.verbose;
	note("Verbosity %sabled\n", (Debug.verbose) ? "en" : "dis");
}

void
debug_set_offset(pix_t offset)
{
	Debug.offset = offset;
}

pix_t
debug_offset(void)
{
	return (Debug.offset);
}

bool
debug_enabled(debug_area_t area)
{
	return ((Debug.areas & area) != 0);
}

/*
 * Called to toggle debugging for the subsystem described by "key".
 */
void
debug_toggle(unsigned char key)
{
	debug_toggle_t	*const	dt = &Debug.toggles[key];

	if (dt->comment != NULL) {
		if (dt->area != 0) {
			Debug.areas ^= dt->area;
			note("Debug %sabled for %s\n",
			    (Debug.areas & dt->area) ? "en" : "dis",
			    dt->comment);
		}

		if (dt->cb != NULL) {
			(*dt->cb)();
		}
	} else {
		warn("unknown debugging key \"%c\"\n", key);
	}
}

/*
 * Make all the debugging states either enabled or disabled.
 * (In that sense, this is not a strict XOR-like toggle for each state.)
 */
static void
debug_toggle_all(void)
{
	Debug.alldebug = !Debug.alldebug;

	const int	enabled = Debug.alldebug;
	for (int key = 0; key <= UCHAR_MAX; key++) {
		debug_toggle_t	*const	dt = &Debug.toggles[key];

		/*
		 * dt->area is 0 for pseudo-toggles such as this one.
		 */
		if (dt->comment == NULL || dt->area == 0) {
			continue;
		}

		if (enabled != debug_enabled(dt->area)) {
			debug_toggle(key);
		}
	}
}

/*
 * Print all of the available debugging areas.  Invoked with "D ?".
 */
static void
debug_help(void)
{
	note("Debug controls:\n\n");

	note("key  description\n");
	note("---  -----------------\n");

	for (int key = 0; key <= UCHAR_MAX; key++) {
		const char	*comment = Debug.toggles[key].comment;

		if (comment != NULL) {
			note("  %c  %s\n", key, comment);
		}
	}
}

/*
 * Register a subsystem's debug processing.  See comment in debug.h,
 * as well as at the top of this file.
 */
void
debug_register_toggle(unsigned char key, const char *comment,
    debug_area_t area, void (*cb)())
{
	debug_toggle_t	*const	dt = &Debug.toggles[key];

	if (dt->comment != 0) {
		warn("Debug key '%c' is already registered!\n", key);
		warn("  old definition: %s\n",
		    dt->comment != NULL ? dt->comment : "<empty>");
		warn("  new definition: %s\n",
		    comment != NULL ? comment : "<empty>");
	} else {
		dt->comment = comment;
		dt->area = area;
		dt->cb = cb;

		Debug.allareas |= area;
		Debug.initialized = true;

		/*
		 * If we were passed in some subsystems to initialize
		 * on the command line, see if the current subsystem is
		 * among that list.  If so, pull it out of the list, and
		 * turn the subsystem on.
		 */
		if (Debug.initstr != NULL) {
			char	*const	initstr = Debug.initstr;
			char		*s;

			if (strchr(initstr, key) != NULL) {
				assert(!debug_enabled(area));
				debug_toggle(key);
			}

			while ((s = strchr(Debug.initstr, key)) != NULL) {
				const int	ninit = Debug.ninit;

				assert(ninit > 0);
				assert(s < &initstr[ninit]);
				assert(initstr[ninit] == '\0');
				assert(initstr[ninit - 1] != '\0');
				if (s != &initstr[ninit - 1]) {
					*s = initstr[ninit - 1];
				}
				initstr[ninit - 1] = '\0';
				Debug.ninit--;
			}

			if (Debug.ninit == 0) {
				mem_free((void **)&Debug.initstr);
			}
		}
	}
}

/* ------------------------------------------------------------------ */

void
debug(debug_area_t area, const char *format, ...)
{
	va_list alist;

	assert((Debug.allareas & area) || !Debug.initialized);

	if (Debug.areas & area) {
		va_start(alist, format);
		(void) vfprintf(stdout, format, alist);
		va_end(alist);
	}
}

void
verbose(debug_area_t area, const char *format, ...)
{
	va_list alist;

	if (Debug.verbose || (Debug.areas & area)) {
		va_start(alist, format);
		(void) vfprintf(stdout, format, alist);
		va_end(alist);
	}
}

void
note(const char *format, ...)
{
	va_list alist;

	va_start(alist, format);
	(void) vfprintf(stdout, format, alist);
	va_end(alist);
}

void
warn(const char *format, ...)
{
	int err = errno;
	va_list alist;

	va_start(alist, format);
	(void) vfprintf(stderr, format, alist);
	va_end(alist);

	if (strchr(format, '\n') == NULL) {
		(void) fprintf(stderr, ": %s\n", strerror(err));
	}
}

void
die(const char *format, ...)
{
	int err = errno;
	va_list alist;

	va_start(alist, format);
	(void) vfprintf(stderr, format, alist);
	va_end(alist);

	if (strchr(format, '\n') == NULL) {
		(void) fprintf(stderr, ": %s\n", strerror(err));
	}

	exit(1);
}

void
ocl_die(int err, const char *format, ...)
{
	va_list alist;

	va_start(alist, format);
	(void) vfprintf(stderr, format, alist);
	va_end(alist);

	/*
	 * Sadly, I don't know of an easy way to turn this into a more
	 * useful error message.
	 */
	(void) fprintf(stderr, ": OpenCL error %d\n", err);

	exit(1);
}
