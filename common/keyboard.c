/*
 * keyboard.c - handles keyboard input.
 */
#include <assert.h>
#include <limits.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>

#include "common.h"

#include "debug.h"
#include "osdep.h"
#include "keyboard.h"
#include "module.h"
#include "util.h"
#include "window.h"

static void	key_help(void);
static void	keyboard_cb(unsigned char key);

/* ------------------------------------------------------------------ */

typedef enum {
	CM_NORMAL		= 0,	/* a regular callback */
	CM_CAPTURE_ONESHOT	= 1,	/* send next key to cb */
	CM_CAPTURE_TOGGLE	= 2	/* send all keys to cb until toggled */
} cap_mode_t;

typedef struct {
	void		(*cb)(int, int);	/* a callback */
	const char	*comment;		/* brief description */
	cap_mode_t	cap_mode;		/* does this grab next key? */
	int		arg1;			/* callback args */
	int		arg2;
	bool		is_param;		/* is this a parameter tweak? */
} keyboard_cb_t;

static struct {
	keyboard_cb_t	cbs[KB_NUM_BINDINGS][UCHAR_MAX + 1];

	key_binding_type_t kb;
	keyboard_cb_t	*captured;
	unsigned char	captured_key;
} Keyboard;

typedef struct {
	unsigned char	key;
	time_t		ts;
} logent_t;

static struct {
	bool		active;
	int		fd;
} Keylog;

/* ------------------------------------------------------------------ */

/*
 * Note: other preinit functions may have called key_register*() before
 * keyboard_preinit() is called.  So keyboard_preinit() shouldn't be used
 * to initialize data needed by key_register*().
 */
static void
keyboard_preinit(void)
{
	key_register('?', KB_DEFAULT, "display this help", key_help);

	Keyboard.kb = KB_DEFAULT;
	Keyboard.captured = NULL;
	Keyboard.captured_key = '\0';

	window_set_keyboard_cb(keyboard_cb);
}

const module_ops_t	keyboard_ops = {
	keyboard_preinit,
	NULL,
	NULL
};

/* ------------------------------------------------------------------ */

/*
 * If I wanted to be clever, I'd come up with a callout-like mechanism that
 * would maintain a log of these, and only flush them once five seconds had
 * passed since the last keystroke. But rather than reinventing an entire
 * operating system here, I'm just gonna write stuff directly.
 */
static void
keylog_add(unsigned char key)
{
	logent_t	le;

	le.key = key;
	le.ts = time(NULL);
	write(Keylog.fd, &le, sizeof (logent_t));
}

void
keylog_start(void)
{
	char			logfile[80];
	struct sigaction	sa;

	Keylog.active = true;
	snprintf(logfile, sizeof (logfile), "keys.%llu", gethrtime());
	Keylog.fd = open(logfile, O_WRONLY|O_CREAT|O_EXCL, 0600);
}

/* ------------------------------------------------------------------ */

static void
keyboard_cb(unsigned char key)
{
	if (Keylog.active) {
		keylog_add(key);
	}

	/*
	 * This is the hook that allows callbacks registered with
	 * key_register_cb_*() to work.
	 *
	 * When their key is pressed, rather than vectoring directly to the
	 * callback, it sets up the callback as the thing to be called for
	 * the next keypress.
	 */
	if (Keyboard.captured != NULL) {
		keyboard_cb_t	*kcb = Keyboard.captured;
		krcb_t		*cb = (krcb_t *)kcb->cb;

		assert(kcb->cap_mode == CM_CAPTURE_ONESHOT ||
		       kcb->cap_mode == CM_CAPTURE_TOGGLE);

		if (kcb->cap_mode == CM_CAPTURE_ONESHOT) {
			Keyboard.captured = NULL;
			Keyboard.captured_key = '\0';
			(*cb)(Keyboard.kb, key);
		} else if (kcb->cap_mode == CM_CAPTURE_TOGGLE) {
			if (key == Keyboard.captured_key) {
				Keyboard.captured = NULL;
				Keyboard.captured_key = '\0';
			} else {
				(*cb)(Keyboard.kb, key);
			}
		}
	} else {
		keyboard_cb_t	*kcb = &Keyboard.cbs[Keyboard.kb][key];

		if (kcb->cb != NULL) {
			if (kcb->cap_mode == CM_NORMAL) {
				(*kcb->cb)(kcb->arg1, kcb->arg2);
			} else {
				Keyboard.captured = kcb;
				Keyboard.captured_key = key;
			}
		}
	}
}

void
key_process(const char *keys)
{
	for (const char *k = keys; *k != '\0'; k++) {
		keyboard_cb(*k);
	}
}

static void
key_help_pass(bool is_param)
{
	note("   key  description\n");
	note("------  --------------------------------------\n");

	for (int key = 0; key <= UCHAR_MAX; key++) {
		keyboard_cb_t	*kcb = &Keyboard.cbs[Keyboard.kb][key];

		if (kcb->comment == NULL || kcb->is_param != is_param) {
			continue;
		}

		switch (key) {
		case '\n':
			note("return");
			break;
		case ('[' - 0x40):
			note("escape");
			break;
		case (' '):
			note(" space");
			break;
		default:
			/*
			 * control characters - ASCII magic
			 */
			if ((key & ~037) == 0) {
				note("    ^%c", key + 0x40);
			} else {
				note("     %c", key);
			}
			break;
		}
		note("  %s\n", kcb->comment);
	}
}

static void
key_help(void)
{
	note("Keyboard controls:\n\n");
	key_help_pass(false);

	note("\nKeyboard parameter controls:\n\n");
	key_help_pass(true);
}

/* ------------------------------------------------------------------ */

key_binding_type_t
key_get_binding(void)
{
	return (Keyboard.kb);
}

void
key_set_binding(key_binding_type_t kb)
{
	assert(kb < KB_NUM_BINDINGS);

	Keyboard.kb = kb;
}

static void
key_register_internal(unsigned char key, key_binding_type_t kb,
    const char *comment, cap_mode_t cm,
    void (*cb)(int, int), int arg1, int arg2, bool is_param)
{
	keyboard_cb_t	*kcb = &Keyboard.cbs[kb][key];

	if (kcb->cb != NULL) {
		warn("Key '%c' is already registered!\n", key);
		warn("  old definition: %s\n",
		    kcb->comment != NULL ? kcb->comment : "<empty>");
		warn("  new definition: %s\n",
		    comment != NULL ? comment : "<empty>");
	} else {
		char	*p;

		if (comment == NULL) {
			p = NULL;
		} else {
			p = mem_alloc(strlen(comment) + 1);
			(void) strcpy(p, comment);
		}

		kcb->cb = cb;
		kcb->comment = p;
		kcb->cap_mode = cm;
		kcb->arg1 = arg1;
		kcb->arg2 = arg2;
		kcb->is_param = is_param;
	}
}

/*
 * For registering a parameter-tweak callback.
 *
 * No reason we couldn't have a key_register_args() that takes two args
 * and isn't a param callback; we just don't have need for one right now.
 */
void
key_register_param(unsigned char key, key_binding_type_t kb,
    const char *comment, void (*cb)(int, int), int arg1, int arg2)
{
	key_register_internal(key, kb, comment, CM_NORMAL,
	    cb, arg1, arg2, true);
}

void
key_register_arg(unsigned char key, key_binding_type_t kb,
    const char *comment, void (*cb)(int), int arg)
{
	key_register_internal(key, kb, comment, CM_NORMAL,
	    (void (*)(int, int))cb, arg, 0, false);
}

void
key_register(unsigned char key, key_binding_type_t kb,
    const char *comment, void (*cb)(void))
{
	key_register_internal(key, kb, comment, CM_NORMAL,
	    (void (*)(int, int))cb, 0, 0, false);
}

void
key_register_cb_oneshot(unsigned char key, key_binding_type_t kb,
    const char *comment, krcb_t *cb)
{
	key_register_internal(key, kb, comment, CM_CAPTURE_ONESHOT,
	    (void (*)(int, int))cb, 0, 0, false);
}

void
key_register_cb_toggle(unsigned char key, key_binding_type_t kb,
    const char *comment, krcb_t *cb)
{
	key_register_internal(key, kb, comment, CM_CAPTURE_TOGGLE,
	    (void (*)(int, int))cb, 0, 0, false);
}
