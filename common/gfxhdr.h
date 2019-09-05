/*
 * gfxhdr.h - pulls in all header files needed for graphics and OpenCL.
 *
 * Different systems keep the various graphics/compute package header files
 * in different locations.
 */

#ifndef	_GFXHDR_H
#define	_GFXHDR_H

#if	defined(__APPLE__)

#define	GL_SILENCE_DEPRECATION

#include <OpenCL/opencl.h>

#ifdef GL3_PROTOTYPES
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#else
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <GLUT/glut.h>
#endif

#elif	defined(__linux__)

#define	GL_GLEXT_PROTOTYPES

#include <CL/opencl.h>
#include <GL/gl.h>
#include <GL/glx.h>
#include <GL/glext.h>
#include <GL/freeglut.h>

#else
#error	this platform is not supported
#endif

#endif	/* _GFXHDR_H */
