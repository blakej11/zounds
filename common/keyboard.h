/*
 * keyboard.h - interfaces for keyboard control.
 */

#ifndef	_KEYBOARD_H
#define	_KEYBOARD_H

#include "types.h"

/*
 * Indicate which type of keyboard we're using - a regular one (KB_DEFAULT)
 * or a dedicated keypad (KB_KEYPAD).
 */
extern void
key_set_binding(key_binding_type_t kb);

extern key_binding_type_t
key_get_binding(void);

/*
 * Register key bindings.
 *
 * When key "key" is pressed and we're using a "kb"-type keyboard,
 * invoke callback "cb", passing the specified zero, one, or two arguments.
 * "comment" is a brief description of what the key binding does,
 * which is displayed when the "?" key is pressed.
 */
extern void
key_register(unsigned char key, key_binding_type_t kb,
    const char *comment, void (*cb)(void));

extern void
key_register_arg(unsigned char key, key_binding_type_t kb,
    const char *comment, void (*cb)(int), int arg);

extern void
key_register_param(unsigned char key, key_binding_type_t kb,
    const char *comment, void (*cb)(int, int), int arg1, int arg2);

/* The type of callback called by key_register_cb*(). */
typedef void (krcb_t)(key_binding_type_t, unsigned char);

extern void
key_register_cb_oneshot(unsigned char key, key_binding_type_t kb,
    const char *comment, krcb_t *cb);

extern void
key_register_cb_toggle(unsigned char key, key_binding_type_t kb,
    const char *comment, krcb_t *cb);

/* ------------------------------------------------------------------ */

extern void
keylog_start(void);

extern void
key_process(const char *keys);

#endif
/* _KEYBOARD_H */
