/*
 * camera.c - routines for interfacing with a video camera.
 */
#include <stdlib.h>
#include <strings.h>
#include <assert.h>

#include "common.h"

#include "debug.h"
#include "image.h"
#include "camera.h"
#include "util.h"

/*
 * load_camera_cb() is used to initialize our data using an image from
 * a built-in camera.  Unsurprisingly, there's quite a lot of work involved
 * in doing this, so we rely on the OpenCV library to do the heavy lifting.
 */
#ifdef OPENCV_SUPPORT

#define cvRound(x) (x)	/* prevents compiler warning */
#include "opencv2/videoio/videoio_c.h"

/*
 * Apparently it's common for webcam-like things to return their results
 * in BGR order rather than RGB.  Thankfully, image_copy() makes it
 * pretty easy to handle that.
 */
static void
bgr_to_rgba(const uint8_t *bgr, uint8_t *rgba, pix_t op, pix_t np)
{
	rgba[4 * np + 0] = bgr[3 * op + 2];
	rgba[4 * np + 1] = bgr[3 * op + 1];
	rgba[4 * np + 2] = bgr[3 * op + 0];
	rgba[4 * np + 3] = 0;
}

bool
load_camera_cb(pix_t width, pix_t height, uint8_t *rgba)
{
	CvCapture	*camera;
	IplImage	*frame;
	CvMat		mat;
	uint8_t		*bgr;

	verbose(DB_IMAGE, "Loading image from camera\n");

	camera = cvCreateCameraCapture(CV_CAP_ANY);
	if (camera == NULL) {
		warn("failed to initialize camera\n");
		return (false);
	}

	frame = cvQueryFrame(camera);
	if (frame == NULL) {
		warn("failed to read from camera\n");
		cvReleaseCapture(&camera);
		return (false);
	}

	/*
	 * This is probably abusing the OpenCV interface somewhat. *shrug*
	 */
	(void) cvGetMat(frame, &mat, NULL, 0);
	bgr = (uint8_t *)CV_MAT_ELEM_PTR(mat, 0, 0);

	bzero(rgba, width * height * 4 * sizeof (char));

	image_copy(mat.cols, mat.rows, bgr, width, height, rgba, bgr_to_rgba);

	cvReleaseCapture(&camera);
	return (true);
}

#else

bool
load_camera_cb(pix_t width, pix_t height, uint8_t *pixels)
{
	return (false);
}

#endif	/* OPENCV_SUPPORT */
