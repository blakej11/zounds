/*
 * opencl.h - wrappers around OpenCL functions.
 *
 * These can be called starting at a module's init() and ending at the
 * module's fini(). Specifically, they assume that opencl_preinit() has
 * been called; that is responsible for setting up all of the necessary
 * OpenCL context.
 */

#ifndef _OPENCL_H
#define _OPENCL_H

#include "types.h"

/* ------------------------------------------------------------------ */

/*
 * Get the name of the GPU device and device vendor.
 * The returned string must not be freed.
 */
const char *
opencl_device_name(void);

const char *
opencl_device_vendor(void);

/*
 * Get the maximum workgroup size for the current device.
 */
size_t
opencl_device_maxwgsize(void);

/* ------------------------------------------------------------------ */

typedef struct {
	cl_kernel	kd_kernel;
	const char	*kd_method;
	size_t		kd_wgsize;
	size_t		kd_maxitems[2];
} kernel_data_t;

/*
 * Create a computation kernel. "kd" holds the relevant metadata;
 * "method" is the name of the OpenCL method.
 */
extern void
kernel_create(kernel_data_t *kd, const char *method);

/*
 * Get the size of the workgroup used for this kernel.
 */
extern size_t
kernel_wgsize(kernel_data_t *kd);

/*
 * Set an argument in preparation for invoking this kernel.
 * "arg" is the argument number, starting at 0.  "size" is the size of the
 * argument, and "value" is a pointer to the argument. The pointed-to
 * argument must not be a constant, even if it's used read-only (sadly).
 */
extern void
kernel_setarg(kernel_data_t *kd, int arg, size_t size, void *value);

/*
 * Invoke the specified OpenCL kernel.
 *
 * "dim" is the number of dimensions for the workgroup.
 * "global" is the total number of work entries to do; it must be an array
 * with "dim" dimensions.
 * "local" is the size of workgroup to use; it must be an array with "dim"
 * dimensions.
 *
 * If "global" is NULL, it will default to using { Width, Height } - though
 * those values are rounded up to the size of the workgroup in that dimension.
 *
 * If "local" is NULL, OpenCL will choose the size of the workgroup.
 */
extern void
kernel_invoke(kernel_data_t *kd, int dim, size_t *global, size_t *local);

/*
 * Wait for all outstanding kernel operations to complete.
 * Useful for gathering performance data.
 */
extern void
kernel_wait(void);

/*
 * Release all resources associated with "kd". "kd" must not be used after
 * this is called.
 */
extern void
kernel_cleanup(kernel_data_t *kd);

/* ------------------------------------------------------------------ */

/*
 * Allocate a GPU buffer of the specified size.
 */
extern cl_mem
buffer_alloc(size_t size);

/*
 * Copy the buffer at "hostsrc" to the GPU buffer at "gpudst".
 */
extern void
buffer_writetogpu(const void *hostsrc, cl_mem gpudst, size_t size);

/*
 * Copy the GPU buffer at "src" to the GPU buffer at "dst".
 */
extern void
buffer_copy(cl_mem src, cl_mem dst, size_t size);

/*
 * Fill the GPU buffer at "dst" with the byte pattern pointed to by "pattern".
 */
extern void
buffer_fill(cl_mem dst, size_t size, void *pattern, size_t pattern_size);

/*
 * Copy the GPU buffer at "gpusrc" to the buffer at "hostdst".
 */
extern void
buffer_readfromgpu(const cl_mem gpusrc, void *hostdst, size_t size);

/*
 * Read a single floating-point number at index "off" from the GPU buffer
 * at "gpusrc". Useful for debugging.
 */
extern float
buffer_float_at(cl_mem gpusrc, pix_t off);

/*
 * Read a single floating-point number from the GPU buffer at "gpusrc".
 * Uses debug_offset() to choose which value to read.
 */
extern float
buffer_float_dbg(cl_mem gpusrc);

/*
 * Read a single datavec at index "off" from the GPU buffer at "gpusrc".
 */
extern cl_datavec
buffer_datavec_at(cl_mem gpusrc, pix_t off);

/*
 * Read a single datavec from the GPU buffer at "gpusrc".
 * Uses debug_offset() to choose which value to read.
 */
extern cl_datavec
buffer_datavec_dbg(cl_mem gpusrc);

/*
 * Read 800 datavecs, and average their first component as integers.
 */
float
buffer_datavec_sumup(cl_mem gpusrc);

/*
 * Free the GPU buffer pointed to by "buf".  (note, unlike the rest of these
 * routines, this takes a pointer to the buffer)
 */
extern void
buffer_free(cl_mem *buf);

/* ------------------------------------------------------------------ */

/*
 * Create an OpenCL image2d_t (in GPU memory) that can hold the type of data
 * specified by "order" and "datatype".
 */
extern cl_mem
ocl_image_create(cl_channel_order order, cl_channel_type datatype,
    pix_t width, pix_t height);

/*
 * Create an OpenCL image2d_t that can hold datavec's.
 */
extern cl_mem
ocl_datavec_image_create(pix_t width, pix_t height);

/*
 * Copy the OpenCL image2d_t at "gpusrc" to the host buffer at "hostdst".
 */
extern void
ocl_image_readfromgpu(const cl_mem gpusrc, void *hostdst,
    pix_t width, pix_t height);

/*
 * Copy the host buffer at "hostsrc" to the OpenCL image2d_t at "gpudst".
 */
extern void
ocl_image_writetogpu(const void *hostsrc, cl_mem gpudst,
    pix_t width, pix_t height);

/*
 * Return the unsigned int at the offset given by debug_offset()
 * from the OpenCL image2d_t at "gpusrc".
 */
extern unsigned int
ocl_image_dbgval(cl_mem gpusrc);

/*
 * Return the datavec at the offset given by debug_offset()
 * from the OpenCL image2d_t at "gpusrc".
 */
extern cl_datavec
ocl_image_dbgdatavec(cl_mem gpusrc);

/*
 * Copy the OpenCL image2d_t from "src" the image2d_t at "dst".
 */
extern void
ocl_image_copy(cl_mem src, cl_mem dst, pix_t width, pix_t height);

/*
 * Copy the contents of the OpenCL buffer at "src" to the OpenCL image2d_t
 * at "image". The buffer must have a size given by
 * width * height * (size of one element, as specified to ocl_image_create()).
 */
extern void
ocl_image_copyfrombuf(cl_mem src, cl_mem image, pix_t width, pix_t height);

/*
 * Copy the contents of the OpenCL image2d_t at "image" to the OpenCL buffer
 * at "dst". The buffer must have a size given by
 * width * height * (size of one element, as specified to ocl_image_create()).
 */
extern void
ocl_image_copytobuf(cl_mem image, cl_mem dst, pix_t width, pix_t height);

/* ------------------------------------------------------------------ */

/*
 * Create an OpenCL image2d_t from an OpenGL texture.
 */
extern cl_mem
clgl_makeimage(uint32_t TextureTarget, uint32_t TextureId);

/*
 * Acquire OpenCL access to the specified CL/GL image.
 * Graphics rendering must not take place while OpenCL has access to it.
 */
extern void
clgl_cl_acquire(cl_mem image);

/*
 * Release OpenCL access to the specified CL/GL image.
 * This allows graphics rendering to take place.
 */
extern void
clgl_cl_release(cl_mem image);

/* ------------------------------------------------------------------ */

#endif	/* _OPENCL_H */
