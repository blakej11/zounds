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
static void	key_D(void);
static void	keyboard_cb(unsigned char key);

/* ------------------------------------------------------------------ */

typedef struct {
	void		(*cb)(int, int);	/* a callback */
	const char	*comment;		/* brief description */
	int		arg1;			/* callback args */
	int		arg2;
} keyboard_cb_t;

static struct {
	keyboard_cb_t	cbs[KB_NUM_BINDINGS][UCHAR_MAX + 1];

	key_binding_type_t kb;
	bool		debug_key;	/* whether next key goes to debug */
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
	key_register('D', KB_DEFAULT, "toggle debug area", key_D);

	/*
	 * As with 'q', this isn't something the keypad has, but
	 * it's nice to be able to do it if I plug in a real keyboard.
	 */
	key_register('D', KB_KEYPAD, "toggle debug area", key_D);

	Keyboard.debug_key = false;
	Keyboard.kb = KB_DEFAULT;

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

	if (Keyboard.debug_key) {
		/*
		 * This is a special hook to let the debug subsystem
		 * track keypresses separately from here.  Typing "D"
		 * invokes key_D(), which sets Keyboard.debug_key = true.
		 * Then this code passes the keypress over to the debug
		 * subsystem.
		 */
		Keyboard.debug_key = false;
		debug_toggle(key);
	} else {
		keyboard_cb_t	*kcb = &Keyboard.cbs[Keyboard.kb][key];
		if (kcb->cb != NULL) {
			(*kcb->cb)(kcb->arg1, kcb->arg2);
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
key_D(void)
{
	assert(!Keyboard.debug_key);

	Keyboard.debug_key = true;
}

static void
key_help(void)
{
	note("Keyboard controls:\n\n");

	note("   key  description\n");
	note("------  --------------------------------------\n");

	for (int key = 0; key <= UCHAR_MAX; key++) {
		keyboard_cb_t	*kcb = &Keyboard.cbs[Keyboard.kb][key];

		if (kcb->comment == NULL) {
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

/* ------------------------------------------------------------------ */

void
key_set_binding(key_binding_type_t kb)
{
	assert(kb < KB_NUM_BINDINGS);

	Keyboard.kb = kb;
}

void
key_register_args(unsigned char key, key_binding_type_t kb,
    const char *comment, void (*cb)(int, int), int arg1, int arg2)
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
		kcb->arg1 = arg1;
		kcb->arg2 = arg2;
	}
}

void
key_register_arg(unsigned char key, key_binding_type_t kb,
    const char *comment, void (*cb)(int), int arg)
{
	key_register_args(key, kb, comment, (void (*)(int, int))cb, arg, 0);
}

void
key_register(unsigned char key, key_binding_type_t kb,
    const char *comment, void (*cb)(void))
{
	key_register_args(key, kb, comment, (void (*)(int, int))cb, 0, 0);
}
