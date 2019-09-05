/*
 * module.c - invoke all of the subsystem-specific init and fini functions.
 * See the module_ops_t definition in module.h for more details.
 */
#include "common.h"

#include "module.h"
#include "opencl.h"	/* for kernel_wait() */

/* ------------------------------------------------------------------ */

/*
 * The lists (sadly plural) of all the module_ops_t's.
 * Any new modules should be added here.
 */

extern const module_ops_t	basis_ops;
extern const module_ops_t	box_ops;
extern const module_ops_t	core_ops;
extern const module_ops_t	datasrc_ops;
extern const module_ops_t	debug_ops;
extern const module_ops_t	heatmap_ops;
extern const module_ops_t	histogram_ops;
extern const module_ops_t	image_ops;
extern const module_ops_t	interp_ops;
extern const module_ops_t	keyboard_ops;
extern const module_ops_t	mouse_ops;
extern const module_ops_t	opencl_ops;
extern const module_ops_t	param_ops;
extern const module_ops_t	skip_ops;
extern const module_ops_t	stroke_ops;
extern const module_ops_t	window_ops;

static const module_ops_t *Modules[] = {
	&basis_ops,
	&box_ops,
	&core_ops,
	&datasrc_ops,
	&debug_ops,
	&heatmap_ops,
	&histogram_ops,
	&image_ops,
	&interp_ops,
	&keyboard_ops,
	&mouse_ops,
	&opencl_ops,
	&param_ops,
	&skip_ops,
	&stroke_ops,
	&window_ops
};

/* ------------------------------------------------------------------ */

void
module_preinit(void)
{
	const size_t	nmod = (sizeof (Modules) / sizeof (*Modules));
	size_t		i;

	for (i = 0; i < nmod; i++) {
		if (Modules[i]->preinit != NULL) {
			(*Modules[i]->preinit)();
		}
	}
}

void
module_init(void)
{
	const size_t	nmod = (sizeof (Modules) / sizeof (*Modules));
	size_t		i;

	for (i = 0; i < nmod; i++) {
		if (Modules[i]->init != NULL) {
			(*Modules[i]->init)();
		}
	}
}

void
module_fini(void)
{
	const size_t	nmod = (sizeof (Modules) / sizeof (*Modules));
	size_t		i;

	kernel_wait();

	for (i = 0; i < nmod; i++) {
		if (Modules[i]->fini != NULL) {
			(*Modules[i]->fini)();
		}
	}
}

void
module_postfini(void)
{
	const size_t	nmod = (sizeof (Modules) / sizeof (*Modules));
	size_t		i;

	for (i = 0; i < nmod; i++) {
		if (Modules[i]->postfini != NULL) {
			(*Modules[i]->postfini)();
		}
	}
}
