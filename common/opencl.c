/*
 * opencl.c - useful wrappers around OpenCL functions.
 *
 * A lot of what these do is to check the return codes for success;
 * error codes from OpenCL are generally treated as fatal.
 */
#include <stdlib.h>
#include <strings.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <sys/stat.h>

#include "common.h"

#include "debug.h"
#include "gfxhdr.h"
#include "opencl.h"
#include "osdep.h"
#include "module.h"
#include "util.h"
#include "window.h"

/*
 * Pull in the OpenCL sources.
 */
#include "kernelsrc.c"

/* ------------------------------------------------------------------ */

static struct {
	cl_context		context;
	cl_command_queue	commands;
	cl_program		program;
	cl_device_id		deviceid;

	char			device_vendor[1024];
	char			device_name[1024];
	size_t			max_work_items[3];
} Opencl;

/* ------------------------------------------------------------------ */

const char *
opencl_device_vendor(void)
{
	return (Opencl.device_vendor);
}

const char *
opencl_device_name(void)
{
	return (Opencl.device_name);
}

size_t
opencl_device_maxwgsize(void)
{
	return (MIN(Opencl.max_work_items[0], Opencl.max_work_items[1]));
}

/*
 * Initialization code.
 */

static void
report_device(cl_device_id devid, const char *msg)
{
	cl_int		err;
	size_t		returned_size;
	cl_char		vendor_name[1024];
	cl_char		device_name[1024];
	cl_ulong	localmem;
	cl_uint		compute;
	cl_ulong	memalloc;
	cl_char		driver_version[1024];
	cl_bool		unified_mem;
	size_t		max_work_items[3];

	/*
	 * Report device statistics.
	 */
	err = clGetDeviceInfo(devid, CL_DEVICE_VENDOR,
	    sizeof (vendor_name), vendor_name, &returned_size);
	if (err != CL_SUCCESS) {
		ocl_die(err, "Failed to retrieve device vendor");
	}
	err = clGetDeviceInfo(devid, CL_DEVICE_NAME,
	    sizeof (device_name), device_name, &returned_size);
	if (err != CL_SUCCESS) {
		ocl_die(err, "Failed to retrieve device name");
	}
	err = clGetDeviceInfo(devid, CL_DEVICE_LOCAL_MEM_SIZE,
	    sizeof (cl_ulong), &localmem, NULL);
	if (err != CL_SUCCESS) {
		ocl_die(err, "Failed to retrieve local memory size");
	}
	err = clGetDeviceInfo(devid, CL_DEVICE_MAX_COMPUTE_UNITS,
	    sizeof (cl_ulong), &compute, NULL);
	if (err != CL_SUCCESS) {
		ocl_die(err, "Failed to retrieve max compute units");
	}
	err = clGetDeviceInfo(devid, CL_DEVICE_MAX_MEM_ALLOC_SIZE,
	    sizeof (cl_ulong), &memalloc, NULL);
	if (err != CL_SUCCESS) {
		ocl_die(err, "Failed to retrieve max mem alloc size");
	}
	err = clGetDeviceInfo(devid, CL_DRIVER_VERSION,
	    sizeof (driver_version), driver_version, &returned_size);
	if (err != CL_SUCCESS) {
		ocl_die(err, "Failed to retrieve driver version");
	}
	err = clGetDeviceInfo(devid, CL_DEVICE_HOST_UNIFIED_MEMORY,
	    sizeof (cl_bool), &unified_mem, NULL);
	if (err != CL_SUCCESS) {
		ocl_die(err, "Failed to retrieve unified memory status");
	}
	err = clGetDeviceInfo(devid, CL_DEVICE_MAX_WORK_ITEM_SIZES,
	    sizeof (max_work_items), &max_work_items, NULL);
	if (err != CL_SUCCESS) {
		ocl_die(err, "Failed to retrieve kernel work group sizes");
	}

	debug(DB_OPENCL, "%s %s %s...\n", msg, vendor_name, device_name);
	debug(DB_OPENCL, "OpenCL driver version: %s\n", driver_version);
	debug(DB_OPENCL, "Local memory: %lu\n", (unsigned long)localmem);
	debug(DB_OPENCL, "Max compute units: %u\n", (unsigned int)compute);
	debug(DB_OPENCL, "Max mem alloc size: %lu\n", (unsigned long)memalloc);
	debug(DB_OPENCL, "Uses %s memory\n",
	    (unified_mem ? "unified" : "device"));
	debug(DB_OPENCL, "Device work group limits: [%zu, %zu]\n",
	    max_work_items[0], max_work_items[1]);
	debug(DB_OPENCL, "\n");
}

static cl_device_id
create_compute_device(void)
{
	cl_int		err;
	unsigned int	device_count;
	cl_device_id	device_ids[16];
	size_t		returned_size;
	cl_device_id	devid;
	int		i;

	/*
	 * Find a device of type GPU.
	 */
	err = clGetContextInfo(Opencl.context, CL_CONTEXT_DEVICES,
	    sizeof (device_ids), device_ids, &returned_size);
	if (err != 0) {
		ocl_die(err, "Failed to retrieve compute devices for context");
	}
	device_count = returned_size / sizeof (cl_device_id);

	devid = NULL;
	for (i = 0; i < device_count; i++) {
		const cl_device_id	d = device_ids[i];
		cl_device_type		device_type;
		cl_bool			unified_mem;
		cl_bool			image_support;

		report_device(d, "Looking at");

		clGetDeviceInfo(d, CL_DEVICE_TYPE,
		    sizeof (cl_device_type), &device_type, NULL);
		if (device_type != CL_DEVICE_TYPE_GPU) {
			continue;
		}
		err = clGetDeviceInfo(d, CL_DEVICE_HOST_UNIFIED_MEMORY,
		    sizeof (cl_bool), &unified_mem, NULL);
		if (err != CL_SUCCESS) {
			ocl_die(err, "Failed to query unified memory status");
		}
		err = clGetDeviceInfo(d, CL_DEVICE_IMAGE_SUPPORT,
		    sizeof (cl_bool), &image_support, NULL);
		if (err != CL_SUCCESS) {
			ocl_die(err, "Failed to query for image support");
		}

		/*
		 * We can only use GPUs that have image support.
		 *
		 * Choose this one if we haven't seen any yet,
		 * or if it has on-board memory (i.e. we have an eGPU).
		 */
		if (image_support && (devid == NULL || !unified_mem)) {
			devid = d;
		}
	}
	if (devid == NULL) {
		die("Failed to locate compute device\n");
	}

	/* save these for boxparams */
	err = clGetDeviceInfo(devid, CL_DEVICE_VENDOR,
	    sizeof (Opencl.device_vendor), (cl_char *)Opencl.device_vendor,
	    &returned_size);
	if (err != CL_SUCCESS) {
		ocl_die(err, "Failed to retrieve vendor name");
	}
	err = clGetDeviceInfo(devid, CL_DEVICE_NAME,
	    sizeof (Opencl.device_name), (cl_char *)Opencl.device_name,
	    &returned_size);
	if (err != CL_SUCCESS) {
		ocl_die(err, "Failed to retrieve device name");
	}
	err = clGetDeviceInfo(devid, CL_DEVICE_MAX_WORK_ITEM_SIZES,
	    sizeof (Opencl.max_work_items), &Opencl.max_work_items, NULL);
	if (err != CL_SUCCESS) {
		ocl_die(err, "Failed to retrieve kernel work group sizes");
	}

	report_device(devid, "Connecting to");

	return (devid);
}

static cl_program
create_program(void)
{
	const char	*source;
	cl_program	prog;
	cl_int		err;

	source = Kernel_source;			/* from kernelsrc.c */
	prog = clCreateProgramWithSource(Opencl.context, 1,
	    (const char **)&source, NULL, &err);
	if (err != CL_SUCCESS) {
		ocl_die(err, "Failed to create compute program");
	}

	/*
	 * Build the program executable.
	 */
	err = clBuildProgram(prog, 0, NULL, NULL, NULL, NULL);
	if (err != CL_SUCCESS) {
		char	*buffer;
		size_t	len;

		warn("Failed to build program executable\n");

		clGetProgramBuildInfo(prog, Opencl.deviceid,
		    CL_PROGRAM_BUILD_LOG, 0, NULL, &len);
		buffer = mem_alloc(len);

		clGetProgramBuildInfo(prog, Opencl.deviceid,
		    CL_PROGRAM_BUILD_LOG, len, buffer, NULL);
		die("%s\n", buffer);
	}

	return (prog);
}

/*
 * Create an OpenCL context without using OpenGL.
 */
static cl_context
create_cl_context_nogfx(void)
{
	cl_platform_id	platform;
	cl_context	ctx;
	cl_int		err;
	cl_uint		ndev;
	cl_device_id	*devs;

	cl_context_properties *const properties = NULL;

	err = clGetPlatformIDs(1, &platform, NULL);
	if (err != 0) {
		ocl_die(err, "Failed to get platform ID");
	}
	err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 0, NULL, &ndev);
	if (err != CL_SUCCESS) {
		ocl_die(err, "Failed to get device count");
	}
	devs = mem_alloc(ndev * sizeof (cl_device_id));
	err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, ndev, devs, NULL);
	if (err != CL_SUCCESS) {
		ocl_die(err, "Failed to get device list");
	}
	ctx = clCreateContext(properties, 1, &devs[ndev - 1], NULL, NULL, &err);
	if (err != CL_SUCCESS) {
		ocl_die(err, "Failed to create a compute context");
	}
	assert(ctx != NULL);
	mem_free((void **)&devs);

	return (ctx);
}

/*
 * Create all of the OpenCL context needed to use the GPU.
 */
static void
opencl_preinit(void)
{
	int err;

	debug_register_toggle('o', "OpenCL", DB_OPENCL, NULL);

	if (window_graphics()) {
		Opencl.context = create_cl_context();
	} else {
		Opencl.context = create_cl_context_nogfx();
	}
	Opencl.deviceid = create_compute_device();
	Opencl.program = create_program();

	Opencl.commands =
	    clCreateCommandQueue(Opencl.context, Opencl.deviceid, 0, &err);
	if (Opencl.commands == NULL) {
		ocl_die(err, "Failed to create the command queue");
	}
}

/*
 * Tear this context down before we exit.  Probably not strictly needed,
 * but seems wise.
 */
static void
opencl_postfini(void)
{
	clFinish(Opencl.commands);

	clReleaseCommandQueue(Opencl.commands);
	clReleaseProgram(Opencl.program);
	clReleaseContext(Opencl.context);

	bzero(&Opencl, sizeof (Opencl));
}

const module_ops_t	opencl_ops = {
	opencl_preinit,
	NULL,
	NULL,
	opencl_postfini
};

/* ------------------------------------------------------------------ */

/*
 * OpenCL buffers.
 */

cl_mem
buffer_alloc(size_t size)
{
	cl_int	err;
	cl_mem	buf;

	buf = clCreateBuffer(Opencl.context,
	    CL_MEM_READ_WRITE, size, NULL, &err);
	if (err != CL_SUCCESS) {
		ocl_die(err, "Failed to allocate OpenCL array");
	}
	assert(buf != NULL);

	return (buf);
}

void
buffer_writetogpu(const void *hostsrc, cl_mem gpudst, size_t size)
{
	cl_int	err;

	err = clEnqueueWriteBuffer(Opencl.commands,
	    gpudst, CL_TRUE, 0, size, hostsrc, 0, NULL, NULL);
	if (err != CL_SUCCESS) {
		ocl_die(err, "Failed to write buffer to GPU");
	}
}

void
buffer_fill(cl_mem dst, size_t size, void *pattern, size_t pattern_size)
{
	cl_int	err;

	err = clEnqueueFillBuffer(Opencl.commands,
	    dst, pattern, pattern_size, 0, size, 0, NULL, NULL);
	if (err != CL_SUCCESS) {
		ocl_die(err, "Failed to fill buffer");
	}
}

void
buffer_copy(cl_mem src, cl_mem dst, size_t size)
{
	cl_int	err;

	err = clEnqueueCopyBuffer(Opencl.commands,
	    src, dst, 0, 0, size, 0, NULL, NULL);
	if (err != CL_SUCCESS) {
		ocl_die(err, "Failed to copy buffer");
	}
}

void
buffer_readfromgpu(const cl_mem gpusrc, void *hostdst, size_t size)
{
	cl_int	err;

	err = clEnqueueReadBuffer(Opencl.commands,
	    gpusrc, CL_TRUE, 0, size, hostdst, 0, NULL, NULL);
	if (err != CL_SUCCESS) {
		ocl_die(err, "Failed to read buffer from GPU");
	}
}

/*
 * Read a single float from the specified offset of the specified buffer.
 */
float
buffer_float_at(cl_mem gpusrc, pix_t off)
{
	cl_int		err;
	float		v;

	err = clEnqueueReadBuffer(Opencl.commands, gpusrc, CL_TRUE,
	    off * sizeof (float), sizeof (v), &v, 0, NULL, NULL);
	if (err != CL_SUCCESS) {
		ocl_die(err, "Failed to read offset %zu of buffer", off);
	}

	return (v);
}

/*
 * Read a single float from the debug offset of the specified buffer.
 */
float
buffer_float_dbg(cl_mem gpusrc)
{
	return (buffer_float_at(gpusrc, debug_offset()));
}

/*
 * Read a single datavec from the specified offset of the specified buffer.
 */
cl_datavec
buffer_datavec_at(cl_mem gpusrc, pix_t off)
{
	cl_int		err;
	cl_datavec	v;

	err = clEnqueueReadBuffer(Opencl.commands, gpusrc, CL_TRUE,
	    off * sizeof (cl_datavec), sizeof (v), &v, 0, NULL, NULL);
	if (err != CL_SUCCESS) {
		ocl_die(err, "Failed to read offset %zu of buffer", off);
	}

	return (v);
}

/*
 * Read 800 datavecs, and average their first component as integers.
 */
float
buffer_datavec_sumup(cl_mem gpusrc)
{
	cl_datavec	v[800];
	const int	N = sizeof (v) / sizeof (*v);
	cl_int		err;

	err = clEnqueueReadBuffer(Opencl.commands, gpusrc, CL_TRUE,
	    0, sizeof (v), v, 0, NULL, NULL);
	if (err != CL_SUCCESS) {
		ocl_die(err, "Failed to read offset %zu of buffer", 0);
	}

	int	sum = 0;
	for (int i = 0; i < N; i++) {
		sum += (int)v[i].s[0];
	}

	return ((float)sum / (float)N);
}

/*
 * Read a single datavec from the debug offset of the specified buffer.
 */
cl_datavec
buffer_datavec_dbg(cl_mem gpusrc)
{
	return (buffer_datavec_at(gpusrc, debug_offset()));
}

void
buffer_free(cl_mem *buf)
{
	clReleaseMemObject(*buf);
	*buf = NULL;
}

/* ------------------------------------------------------------------ */

/*
 * OpenCL kernels.
 */

void
kernel_create(kernel_data_t *kd, const char *method)
{
	cl_int	err;
	size_t	max_wg_size;

	kd->kd_method = method;

	kd->kd_kernel = clCreateKernel(Opencl.program, method, &err);
	if (err != CL_SUCCESS) {
		ocl_die(err, "Failed to create compute kernel");
	}
	assert(kd->kd_kernel != NULL);

	/*
	 * Get the maximum work group size for executing the kernel on the
	 * device.
	 */
	err = clGetKernelWorkGroupInfo(kd->kd_kernel, Opencl.deviceid,
	    CL_KERNEL_WORK_GROUP_SIZE, sizeof (size_t), &max_wg_size, NULL);
	if (err != CL_SUCCESS) {
		ocl_die(err, "Failed to retrieve kernel work group info");
	}

	/*
	 * What I really want here is "the size of the local work group
	 * that would be used if I don't specify a size".  'Cause that's
	 * as far as I really want to round it.  But I don't know how to
	 * get that, so...
	 */
	kd->kd_maxitems[0] = Opencl.max_work_items[0];
	kd->kd_maxitems[1] = Opencl.max_work_items[1];
	kd->kd_wgsize = MIN(max_wg_size, Opencl.max_work_items[0]);

	debug(DB_OPENCL, "Kernel \"%s\" workgroup size: max %zu, actual %zu\n",
	    kd->kd_method, max_wg_size, kd->kd_wgsize);
}

size_t
kernel_wgsize(kernel_data_t *kd)
{
	return (kd->kd_wgsize);
}

void
kernel_setarg(kernel_data_t *kd, int arg, size_t size, void *value)
{
	if (clSetKernelArg(kd->kd_kernel, arg, size, value) != 0) {
		die("Failed to set arg #%d in kernel %s\n", arg, kd->kd_method);
	}
}

void
kernel_invoke(kernel_data_t *kd, int dim, size_t *global_arg, size_t *local)
{
	size_t	global_default[2] = {
		P2ROUNDUP((size_t)Width, kd->kd_maxitems[0]),
		P2ROUNDUP((size_t)Height, kd->kd_maxitems[1])
	};
	size_t	*global;
	cl_int	err;

	if (global_arg == NULL) {
		global = global_default;
	} else {
		global = global_arg;
	}

	err = clEnqueueNDRangeKernel(Opencl.commands, kd->kd_kernel,
	    dim, NULL, global, local, 0, NULL, NULL);
	if (err) {
		ocl_die(err, "Failed to enqueue kernel %s", kd->kd_method);
	}
}

void
kernel_wait(void)
{
	clFinish(Opencl.commands);
}

void
kernel_cleanup(kernel_data_t *kd)
{
	clReleaseKernel(kd->kd_kernel);
	bzero(kd, sizeof (*kd));
}

/* ------------------------------------------------------------------ */

/*
 * OpenCL images created from OpenGL textures.
 */

cl_mem
clgl_makeimage(uint32_t TextureTarget, uint32_t TextureId)
{
	cl_mem	image;
	int err = 0;

	if (!window_graphics()) {
		die("clgl_makeimage(): "
		    "can't do anything with graphics disabled\n");
	}
	image = clCreateFromGLTexture2D(Opencl.context,
	    CL_MEM_READ_WRITE, TextureTarget, 0, TextureId, &err);
	if (err != CL_SUCCESS) {
		ocl_die(err, "Failed to create OpenGL texture reference");
	}
	assert(image != NULL);

	return (image);
}

void
clgl_cl_acquire(cl_mem image)
{
	cl_int		err;

	if (!window_graphics()) {
		return;
	}

	err = clEnqueueAcquireGLObjects(Opencl.commands, 1, &image, 0, 0, NULL);
	if (err != CL_SUCCESS) {
		ocl_die(err, "Failed to acquire GL object");
	}
}

void
clgl_cl_release(cl_mem image)
{
	cl_event	ev;
	cl_int		err;

	if (!window_graphics()) {
		return;
	}

	err = clEnqueueReleaseGLObjects(Opencl.commands, 1, &image, 0, 0, &ev);
	if (err != CL_SUCCESS) {
		ocl_die(err, "Failed to release GL object");
	}
	err = clWaitForEvents(1, &ev);
	if (err != CL_SUCCESS) {
		ocl_die(err, "Failed to wait for GL release");
	}
	err = clReleaseEvent(ev);
	if (err != CL_SUCCESS) {
		ocl_die(err, "Failed to release \"GL release\" event");
	}
}

/* ------------------------------------------------------------------ */

/*
 * OpenCL images.
 */

cl_mem
ocl_image_create(cl_channel_order order, cl_channel_type datatype,
    pix_t width, pix_t height)
{
	cl_mem		image;
	cl_image_format	format;
	cl_image_desc	desc;
	cl_int		err;

	bzero(&format, sizeof (format));
	format.image_channel_order	= order;
	format.image_channel_data_type	= datatype;

	bzero(&desc, sizeof (desc));
	desc.image_type			= CL_MEM_OBJECT_IMAGE2D;
	desc.image_width		= width;
	desc.image_height		= height;
	desc.image_depth		= 1;
	desc.image_array_size		= 1;
	desc.image_row_pitch		= 0;
	desc.image_slice_pitch		= 0;
	desc.num_mip_levels		= 0;
	desc.num_samples		= 0;

	image = clCreateImage(Opencl.context, CL_MEM_READ_WRITE,
	    &format, &desc, NULL, &err);
	if (err != CL_SUCCESS) {
		ocl_die(err, "Failed to allocate OpenCL image");
	}
	assert(image != NULL);

	return (image);
}

/*
 * Create an image2d_t that can hold datavec's.
 */
cl_mem
ocl_datavec_image_create(pix_t width, pix_t height)
{
#if	DATA_DIMENSIONS == 1
	const cl_channel_order  order = CL_INTENSITY;
#elif	DATA_DIMENSIONS == 3
	const cl_channel_order  order = CL_RGB;
#elif	DATA_DIMENSIONS == 4
	const cl_channel_order  order = CL_RGBA;
#else
#error  Do not know how to convert data into an image.
#endif

	return (ocl_image_create(order, CL_FLOAT, width, height));
}

void
ocl_image_readfromgpu(const cl_mem gpusrc, void *hostdst,
    pix_t width, pix_t height)
{
	const size_t	origin[] = { 0, 0, 0 };
	const size_t	region[] = { (size_t)width, (size_t)height, 1 };
	cl_int		err;

	err = clEnqueueReadImage(Opencl.commands,
	    gpusrc, CL_TRUE, origin, region, 0, 0, hostdst, 0, NULL, NULL);
	if (err != CL_SUCCESS) {
		ocl_die(err, "Failed to read image from GPU");
	}
}

void
ocl_image_writetogpu(const void *hostsrc, cl_mem gpudst,
    pix_t width, pix_t height)
{
	const size_t	origin[] = { 0, 0, 0 };
	const size_t	region[] = { (size_t)width, (size_t)height, 1 };
	cl_int		err;

	err = clEnqueueWriteImage(Opencl.commands,
	    gpudst, CL_TRUE, origin, region, 0, 0, hostsrc, 0, NULL, NULL);
	if (err != CL_SUCCESS) {
		ocl_die(err, "Failed to write image to GPU");
	}
}

/*
 * Read the single uint32 at offset debug_offset() of the specified image.
 */
unsigned int
ocl_image_dbgval(cl_mem gpusrc)
{
	const pix_t	off = debug_offset();
	const size_t	origin[] = {
		(size_t)(off % Width), (size_t)(off / Width), 0 };
	const size_t	region[] = { 1, 1, 1 };
	cl_int		err;
	unsigned int	v;

	err = clEnqueueReadImage(Opencl.commands,
	    gpusrc, CL_TRUE, origin, region, 0, 0, &v, 0, NULL, NULL);
	if (err != CL_SUCCESS) {
		ocl_die(err, "Failed to read offset %u of image", off);
	}

	return (v);
}

/*
 * Read a cl_datavec at offset debug_offset() of the specified image.
 */
cl_datavec
ocl_image_dbgdatavec(cl_mem gpusrc)
{
	const pix_t	off = debug_offset();
	const size_t	origin[] = {
		(size_t)(off % Width), (size_t)(off / Width), 0 };
	const size_t	region[] = { 1, 1, 1 };
	cl_int		err;
	cl_datavec	v;

	err = clEnqueueReadImage(Opencl.commands,
	    gpusrc, CL_TRUE, origin, region, 0, 0, &v, 0, NULL, NULL);
	if (err != CL_SUCCESS) {
		ocl_die(err, "Failed to read offset %u of image", off);
	}

	return (v);
}

void
ocl_image_copy(cl_mem src, cl_mem dst, pix_t width, pix_t height)
{
	const size_t	origin[] = { 0, 0, 0 };
	const size_t	region[] = { (size_t)width, (size_t)height, 1 };
	cl_int		err;

	err = clEnqueueCopyImage(Opencl.commands,
	    src, dst, origin, origin, region, 0, NULL, NULL);
	if (err != CL_SUCCESS) {
		ocl_die(err, "Failed to copy image");
	}
}

void
ocl_image_copyfrombuf(cl_mem src, cl_mem image, pix_t width, pix_t height)
{
	const size_t	origin[] = { 0, 0, 0 };
	const size_t	region[] = { (size_t)width, (size_t)height, 1 };
	cl_int		err;

	err = clEnqueueCopyBufferToImage(Opencl.commands,
	    src, image, 0, origin, region, 0, NULL, NULL);
	if (err != CL_SUCCESS) {
		ocl_die(err, "Failed to copy buffer to image");
	}
}

void
ocl_image_copytobuf(cl_mem image, cl_mem dst, pix_t width, pix_t height)
{
	const size_t	origin[] = { 0, 0, 0 };
	const size_t	region[] = { (size_t)width, (size_t)height, 1 };
	cl_int		err;

	err = clEnqueueCopyImageToBuffer(Opencl.commands,
	    image, dst, origin, region, 0, 0, NULL, NULL);
	if (err != CL_SUCCESS) {
		ocl_die(err, "Failed to copy image to buffer");
	}
}
