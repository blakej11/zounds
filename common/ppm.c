/*
 * ppm.c - code for reading and writing PPM files.
 *
 * We only support raw files (with header "P6"), and not all-text PPM files
 * (with header "P3").
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>

#include "ppm.h"
#include "debug.h"

/* ------------------------------------------------------------------ */

/*
 * Figure out how large of an image is stored in a PPM file.
 */
bool
ppm_read_sizes(const char *filename, pix_t *widthp, pix_t *heightp)
{
	char		header[80];
	int		width, height;
	int		fd;

	fd = open(filename, O_RDONLY);
	if (fd == -1) {
		warn("Failed to open \"%s\"", filename);
		return (false);
	}
	(void) read(fd, header, sizeof(header));
	if (sscanf(header, "%*3s %u %u", &width, &height) != 2) {
		warn("Failed to parse \"%s\"\n", header);
		close(fd);
		return (false);
	} else {
		close(fd);
		*widthp = (pix_t)width;
		*heightp = (pix_t)height;
		return (true);
	}
}

/*
 * Read a PPM file into a three-byte-per-pixel RGB uint8_t array.
 */
bool
ppm_read_rgb(const char *filename, pix_t tgtwidth, pix_t tgtheight,
    uint8_t *rgb)
{
	char		header[80], magic[4];
	char		*p;
	int		width, height;
	pix_t		npixels;
	int		ncolors, fd, state, i;

	fd = open(filename, O_RDONLY);
	if (fd == -1) {
		warn("Failed to open file \"%s\"", filename);
		return (false);
	}
	(void) read(fd, header, sizeof(header));
	if (sscanf(header, "%3s %u %u %d",
	    magic, &width, &height, &ncolors) != 4) {
		warn("Failed to parse header \"%s\"\n", header);
		close(fd);
		return (false);
	}
	if (strcmp(magic, "P6") != 0 || ncolors > 255) {
		warn("Can't handle header type \"%s\"\n", header);
		close(fd);
		return (false);
	}
	if ((pix_t)width != tgtwidth || (pix_t)height != tgtheight) {
		warn("can't use image \"%s\" -- "
		    "must be %ux%u pixels, found %ux%u\n", filename,
		    tgtwidth, tgtheight, width, height);
		close(fd);
		return (false);
	}

	/*
	 * string parsing in C, wooooo
	 */
	state = 0;
	for (p = header; p < header + sizeof (header) && state < 7; p++) {
		switch (state) {
		case 0:
		case 2:
		case 4:
		case 6:
			if (!isspace(*p))
				continue;
			break;
		case 1:
		case 3:
		case 5:
			if (isspace(*p))
				continue;
			break;
		}
		state++;
	}
	if (state != 7 || *(p - 1) != '\n') {
		warn("failed to parse \"%s\" carefully\n", header);
		close(fd);
		return (false);
	}

	/*
	 * Seek to the actual data portion of the file.
	 */
	lseek(fd, p - header, SEEK_SET);

	/*
	 * Read in the data.
	 */
	npixels = (pix_t)width * (pix_t)height;
	i = read(fd, rgb, npixels * 3);
	if (i != npixels * 3) {
		warn("only read %d bytes of data\n", i);
		close(fd);
		return (false);
	};
	close(fd);

	return (true);
}

/* ------------------------------------------------------------------ */

/*
 * Write a PPM file from a three-byte-per-pixel RGB uint8_t array.
 */
void
ppm_write_rgb(const char *filename, const uint8_t *rgb,
    pix_t width, pix_t height)
{
	int	fd;

	fd = open(filename, O_RDWR | O_CREAT, 0644);
	if (fd == -1) {
		warn("Failed to open file \"%s\"", filename);
	} else {
		char	header[80];
		snprintf(header, sizeof (header),
		    "P6\n%u %u\n255\n", (int)width, (int)height);
		(void) write(fd, header, strlen(header));

		if (write(fd, rgb, width * height * 3) == -1) {
			warn("Failed to write file \"%s\"", filename);
		}
		close(fd);
	}
}
