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
	pix_t		width, height;
	const char	*filename;
} Camera;

void
camera_disable(void)
{
	Camera.disabled = true;
}

bool
camera_disabled(void)
{
	return (Camera.disabled);
}

/* Is this a file that OpenCV can use? */
bool
camera_try_file(const char *filename, pix_t *w, pix_t *h)
{
	CvCapture	*capture;

	capture = cvCreateFileCapture(filename);
	if (capture == NULL) {
		verbose(DB_CAMERA, "camera_try_file(): OpenCV couldn't "
		    "identify \"%s\" as a video file\n", filename);
		return (false);

	} else if ((int)cvGetCaptureProperty(capture,
	    CV_CAP_PROP_FRAME_COUNT) == 0) {
		verbose(DB_CAMERA, "camera_try_file(): OpenCV thinks "
		    "\"%s\" has no frames\n", filename);
		return (false);

	} else {
		*w = (pix_t)cvGetCaptureProperty(capture,
		    CV_CAP_PROP_FRAME_WIDTH);
		*h = (pix_t)cvGetCaptureProperty(capture,
		    CV_CAP_PROP_FRAME_HEIGHT);

		cvReleaseCapture(&capture);
		return (true);
	}
}

void
camera_set_filename(const char *filename)
{
	Camera.filename = filename;
}

bool
camera_init(void)
{
	if (Camera.disabled) {
		return (false);		// and don't print a warning
	}

	if (Camera.filename != NULL) {
		Camera.capture = cvCreateFileCapture(Camera.filename);
		verbose(DB_CAMERA, "Using file \"%s\" as camera input\n",
		    Camera.filename);
	} else {
		Camera.capture = cvCreateCameraCapture(CV_CAP_ANY);
	}
	if (Camera.capture == NULL) {
		warn("camera_init(): failed to initialize camera\n");
		camera_disable();
		return (false);
	}

	Camera.width  = (pix_t)cvGetCaptureProperty(Camera.capture,
	    CV_CAP_PROP_FRAME_WIDTH);
	Camera.height = (pix_t)cvGetCaptureProperty(Camera.capture,
	    CV_CAP_PROP_FRAME_HEIGHT);

	return (true);
}

bool
camera_initialized(void)
{
	return (Camera.capture != NULL);
}

pix_t
camera_width(void)
{
	assert(Camera.width != 0);
	return (Camera.width);
}

pix_t
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
		if (Camera.filename != NULL) {
			const int curframe =
			    (int)cvGetCaptureProperty(Camera.capture,
			    CV_CAP_PROP_POS_FRAMES);
			const int frames =
			    (int)cvGetCaptureProperty(Camera.capture,
			    CV_CAP_PROP_FRAME_COUNT);
			if (curframe > frames) {
				/*
				 * It's a bit sketchy to have a bare call to
				 * exit() in the middle of "library" code,
				 * but at least with the way the code is
				 * structured right now it's doing exactly the
				 * right thing.
				 */
				exit(0);
			}
		}

		warn("camera_retrieve(): failed to read from camera\n");
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
camera_disabled(void)
{
	return (true);
}

bool
camera_try_file(const char *filename, pix_t *w, pix_t *h)
{
	return (false);
}

void
camera_set_filename(const char *filename)
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

pix_t
camera_width(void)
{
	assert(0 && "camera is not supported");
	return (0);
}

pix_t
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
