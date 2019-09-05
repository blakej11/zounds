/*
 * osdep.c - a few routines that may be very different on different platforms.
 * All of the GLUT interaction happens here and in window.c.
 */
#include <stdlib.h>
#include <strings.h>
#include <assert.h>

#include "common.h"
#include "gfxhdr.h"

#include "debug.h"
#include "image.h"
#include "osdep.h"
#include "util.h"

/* ------------------------------------------------------------------ */

/*
 * Solaris has this wonderful function gethrtime(), which just returns
 * a monotonically increasing number of nanoseconds.  It's a lot easier
 * to use than gettimeofday() et al.
 */

#ifndef __sun		/* in case anyone ever tries... */

#include <sys/time.h>

hrtime_t
gethrtime(void)
{
	struct timeval	tp;
	hrtime_t	rv;

	gettimeofday(&tp, NULL);
	rv = ((unsigned long long)tp.tv_sec * 1000000ull + tp.tv_usec) * 1000;

	return (rv);
}

#endif

/* ------------------------------------------------------------------ */

/*
 * create_cl_context() is used to link OpenCL and OpenGL together.
 * Although the two frameworks are related, they deliberately avoid
 * specifying an OS-independent way of coupling the two.
 *
 * These approaches are taken from:
 *	https://stackoverflow.com/questions/26802905
 */

#ifdef __APPLE__

#include <OpenGL/CGLDevice.h>

cl_context
create_cl_context(void)
{
	cl_context	ctx;
	cl_int		err;

	/*
	 * Create a context from a CGL share group.
	 */
	CGLContextObj kCGLContext = CGLGetCurrentContext();
	CGLShareGroupObj kCGLShareGroup = CGLGetShareGroup(kCGLContext);
	cl_context_properties properties[] = {
		CL_CONTEXT_PROPERTY_USE_CGL_SHAREGROUP_APPLE,
		    (cl_context_properties)kCGLShareGroup,
		0
	};

	ctx = clCreateContext(properties, 0, 0,
	    clLogMessagesToStdoutAPPLE, NULL, &err);
	if (err != CL_SUCCESS) {
		ocl_die(err, "Failed to create a compute context");
	}
	assert(ctx != NULL);

	return (ctx);
}

#elif	defined(__linux__)

cl_context
create_cl_context(void)
{
	cl_platform_id	platform;
	cl_context	ctx;
	cl_int		err;
	int		ndev;
	cl_device_id	*devs;

	cl_context_properties properties[] = {
		CL_GL_CONTEXT_KHR,
		    (cl_context_properties)glXGetCurrentContext(),
		CL_GLX_DISPLAY_KHR,
		    (cl_context_properties)glXGetCurrentDisplay(),
		0
	};

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

#else
#error	this platform is not supported
#endif

/* ------------------------------------------------------------------ */

/*
 * create_glut_context() is used to initialize GLUT.
 *
 * On Linux, FreeGLUT is the only GLUT available.  It has explicit extensions
 * for choosing an OpenGL profile version.
 *
 * FreeGLUT on MacOS goes through XQuartz, and XQuartz doesn't have any way
 * to use more modern OpenGL profiles.  So I use the special hacky
 * GLUT_3_2_CORE_PROFILE flag there.
 */

#ifdef __linux__

void
create_glut_context(void)
{
	int	argc = 0;
	char	*argv[] = { NULL };

	glutInit(&argc, argv);
	glutInitContextVersion(4, 1);
	glutInitContextFlags(GLUT_FORWARD_COMPATIBLE | GLUT_DEBUG);
	glutInitContextProfile(GLUT_CORE_PROFILE);
	glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA | GLUT_DEPTH);
}

#elif	defined(__APPLE__)

void
create_glut_context(void)
{
	int	argc = 0;
	char	*argv[] = { NULL };

	glutInit(&argc, argv);
	glutInitDisplayMode(GLUT_3_2_CORE_PROFILE |
	    GLUT_DOUBLE | GLUT_RGBA | GLUT_DEPTH);
}

#else
#error	this platform is not supported
#endif
