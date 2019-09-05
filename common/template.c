/*
 * template.c - generates useful file names for saving images.
 *
 * The user of this program doesn't want to think about what to call any
 * images that are saved, so we pick a filename that's reasonably evocative.
 * The client of this code specifies a directory name to put all of its
 * images in; this way we can potentially save different kinds of images
 * separately.  Underneath that directory, we create a subdirectory based
 * on the time when template_alloc() was called, and create a symlink called
 * "latest" which points to that subdirectory.  (The symlink is just for
 * the convenience of the user, who can e.g. "cd images/latest/" to look at
 * the most recent results.)
 *
 * The name of the generated image is simply derived from the number of
 * images generated since the start of the program.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/errno.h>
#include <sys/stat.h>
#include <sys/time.h>

#include "debug.h"
#include "template.h"

/* ------------------------------------------------------------------ */

struct template {
	char	template[80];
	char	*template_end;
	size_t	template_bytes_left;
};

/*
 * Create a template for naming files.  This makes the relevant subdirectories.
 */
template_t *
template_alloc(char *dirname)
{
	int		rv;
	time_t		tim;
	struct tm	*tm;
	char		timebuf[80];
	char		latestbuf[80];
	template_t	*t;

	t = calloc(sizeof (template_t), 1);

	rv = mkdir(dirname, 0755);
	if (rv == -1 && errno != EEXIST) {
		die("Couldn't create images directory");
	}

	tim = time(NULL);
	tm = localtime(&tim);
	if (tm == NULL) {
		die("Couldn't get current time");
	}
	(void) snprintf(timebuf, sizeof (timebuf),
	    "%04d-%02d-%02d.%02d:%02d:%02d",
	    tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
	    tm->tm_hour, tm->tm_min, tm->tm_sec);

	(void) snprintf(latestbuf, sizeof (latestbuf),
	    "%s/%s", dirname, "latest");
	(void) unlink(latestbuf);
	(void) symlink(timebuf, latestbuf);

	(void) snprintf(t->template, sizeof (t->template),
	    "%s/%s/", dirname, timebuf);
	rv = mkdir(t->template, 0755);
	if (rv == -1) {
		die("Couldn't create current image directory");
	}
	t->template_end = t->template + strlen(t->template);
	t->template_bytes_left = sizeof (t->template) -
	    (t->template_end - t->template + 1);

	return (t);
}

/*
 * Create the name for a new file using the template.
 */
char *
template_name(template_t *t, const char *label, int steps)
{
	if (label != NULL) {
		snprintf(t->template_end, t->template_bytes_left,
		    "%s.%05d.ppm", label, steps);
	} else {
		snprintf(t->template_end, t->template_bytes_left,
		    "%05d.ppm", steps);
	}
	return (t->template);
}
