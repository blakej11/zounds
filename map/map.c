/*
 * map.c - a trivial example of a core algorithm.
 *
 * This is useful as a template to show how to implement a core algorithm,
 * but it can also be used to experiment with other features of the
 * program, such as the heatmap, the mouse strokes, and the camera.
 */

#include <stdio.h>
#include <stdlib.h>

#include "common.h"

#include "core.h"
#include "debug.h"
#include "keyboard.h"
#include "module.h"
#include "opencl.h"
#include "osdep.h"
#include "param.h"

/* ------------------------------------------------------------------ */

static struct {
	core_ops_t	ops;

	cl_mem		data;

	kernel_data_t	render_kernel;
	kernel_data_t	import_kernel;
	kernel_data_t	export_kernel;
	kernel_data_t	unrender_kernel;
} Map;

/* ------------------------------------------------------------------ */

/*
 * The minimum value of any component of a data vector.
 */
float
map_min(void)
{
	return (0.0f);
}

/*
 * The maximum value of any component of a data vector.
 */
float
map_max(void)
{
	return (1.0f);
}

/*
 * Whether datavec's fit into a sphere or a cube.
 */
datavec_shape_t
map_datavec_shape(void)
{
	return (DATAVEC_SHAPE_CUBE);
}

/* ------------------------------------------------------------------ */

static void
map_unrender(cl_mem image, cl_mem data)
{
	kernel_data_t	*const	kd = &Map.unrender_kernel;
	int			arg;

	arg = 0;
	kernel_setarg(kd, arg++, sizeof (pix_t), &Width);
	kernel_setarg(kd, arg++, sizeof (pix_t), &Height);
	kernel_setarg(kd, arg++, sizeof (cl_mem), &image);
	kernel_setarg(kd, arg++, sizeof (cl_mem), &data);
	kernel_wait();
	kernel_invoke(kd, 2, NULL, NULL);
}

static void
map_import(cl_mem src)
{
	kernel_data_t	*const	kd = &Map.import_kernel;
	cl_mem			dst = Map.data;
	int			arg;

	arg = 0;
	kernel_setarg(kd, arg++, sizeof (pix_t), &Width);
	kernel_setarg(kd, arg++, sizeof (pix_t), &Height);
	kernel_setarg(kd, arg++, sizeof (cl_mem), &src);
	kernel_setarg(kd, arg++, sizeof (cl_mem), &dst);
	kernel_invoke(kd, 2, NULL, NULL);
}

static void
map_export(cl_mem dst)
{
	kernel_data_t	*const	kd = &Map.export_kernel;
	cl_mem			src = Map.data;
	int			arg;

	arg = 0;
	kernel_setarg(kd, arg++, sizeof (pix_t), &Width);
	kernel_setarg(kd, arg++, sizeof (pix_t), &Height);
	kernel_setarg(kd, arg++, sizeof (cl_mem), &src);
	kernel_setarg(kd, arg++, sizeof (cl_mem), &dst);
	kernel_invoke(kd, 2, NULL, NULL);
}

static void
map_render(cl_mem data, cl_mem image)
{
	kernel_data_t	*const	kd = &Map.render_kernel;
	int			arg;

	arg = 0;
	kernel_setarg(kd, arg++, sizeof (pix_t), &Width);
	kernel_setarg(kd, arg++, sizeof (pix_t), &Height);
	kernel_setarg(kd, arg++, sizeof (cl_mem), &data);
	kernel_setarg(kd, arg++, sizeof (cl_mem), &image);
	kernel_invoke(kd, 2, NULL, NULL);
}

/* ------------------------------------------------------------------ */

static void
map_preinit(void)
{
	debug_register_toggle('c', "core algorithm", DB_CORE, NULL);
	debug_register_toggle('P', "performance", DB_PERF, NULL);

	Map.ops.unrender = map_unrender;
	Map.ops.import = map_import;
	Map.ops.step_and_export = map_export;
	Map.ops.render = map_render;
	Map.ops.min = map_min;
	Map.ops.max = map_max;
	Map.ops.datavec_shape = map_datavec_shape;
}

static void
map_init(void)
{
	const size_t	datasize = (size_t)Width * Height * sizeof (cl_datavec);

	core_ops_register(&Map.ops);

	Map.data = buffer_alloc(datasize);

	kernel_create(&Map.unrender_kernel, "unrender");
	kernel_create(&Map.import_kernel, "import");
	kernel_create(&Map.export_kernel, "export");
	kernel_create(&Map.render_kernel, "render");
}

static void
map_fini(void)
{
	kernel_cleanup(&Map.render_kernel);
	kernel_cleanup(&Map.export_kernel);
	kernel_cleanup(&Map.import_kernel);
	kernel_cleanup(&Map.unrender_kernel);

	buffer_free(&Map.data);

	core_ops_unregister(&Map.ops);
}

const module_ops_t	core_ops = {
	map_preinit,
	map_init,
	map_fini
};
