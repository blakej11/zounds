/*
 * camera.c - routines for interfacing with a video camera.
 *
 * This code is used to initialize our data using an image from a built-in
 * camera.  Unsurprisingly, there's quite a lot of work involved in doing this,
 * so we rely on the OpenCV library to do the heavy lifting.
 */
#include <stdlib.h>
#include <strings.h>
#include <assert.h>

#include "common.h"

#include "debug.h"
#include "image.h"
#include "camera.h"
#include "util.h"
#include "osdep.h"

#ifdef OPENCV_SUPPORT

#define cvRound(x) (x)	/* prevents compiler warning */
#include "opencv2/videoio/videoio_c.h"

static struct {
	bool		disabled;
	CvCapture	*capture;
	size_t		width, height;
} Camera;

void
camera_disable(void)
{
	Camera.disabled = true;
}

bool
camera_init(void)
{
	if (Camera.disabled) {
		return (false);		// and don't print a warning
	}

	Camera.capture = cvCreateCameraCapture(CV_CAP_ANY);
	if (Camera.capture == NULL) {
		warn("camera_init(): failed to initialize camera\n");
		camera_disable();
		return (false);
	}

	Camera.width  = (size_t)cvGetCaptureProperty(Camera.capture,
	    CV_CAP_PROP_FRAME_WIDTH);
	Camera.height = (size_t)cvGetCaptureProperty(Camera.capture,
	    CV_CAP_PROP_FRAME_HEIGHT);

	return (true);
}

bool
camera_initialized(void)
{
	return (Camera.capture != NULL);
}

size_t
camera_width(void)
{
	assert(Camera.width != 0);
	return (Camera.width);
}

size_t
camera_height(void)
{
	assert(Camera.height != 0);
	return (Camera.height);
}

bool
camera_grab(void)
{
	return (cvGrabFrame(Camera.capture) == 1);
}

uint8_t *
camera_retrieve(void)
{
	IplImage	*frame;
	CvMat		mat;
	uint8_t		*bgr;

	frame = cvRetrieveFrame(Camera.capture, 0);

	if (frame == NULL) {
		warn("failed to read from camera\n");
		bgr = NULL;
	} else {
		/*
		 * This is abusing the OpenCV interface, but
		 * desperate times call for desperate measures.
		 */
		(void) cvGetMat(frame, &mat, NULL, 0);
		bgr = (uint8_t *)CV_MAT_ELEM_PTR(mat, 0, 0);
	}

	return (bgr);
}

void
camera_fini(void)
{
	cvReleaseCapture(&Camera.capture);
	Camera.capture = NULL;
	Camera.width = Camera.height = 0;
}

#else

void
camera_disable(void)
{
}

bool
camera_init(void)
{
	return (false);
}

bool
camera_initialized(void)
{
	return (false);
}

size_t
camera_width(void)
{
	assert(0 && "camera is not supported");
	return (0);
}

size_t
camera_height(void)
{
	assert(0 && "camera is not supported");
	return (0);
}

bool
camera_grab(void)
{
	assert(0 && "camera is not supported");
	return (false);
}

uint8_t *
camera_retrieve(void)
{
	assert(0 && "camera is not supported");
	return (NULL);
}

uint8_t *
camera_query(void)
{
	assert(0 && "camera is not supported");
	return (NULL);
}

void
camera_fini(void)
{
}

#endif	/* OPENCV_SUPPORT */
