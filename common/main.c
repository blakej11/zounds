#include <stdlib.h>
#include <unistd.h>

#include "common.h"

#include "box.h"
#include "camera.h"
#include "debug.h"
#include "image.h"
#include "keyboard.h"
#include "module.h"
#include "param.h"
#include "ppm.h"
#include "randbj.h"
#include "subblock.h"
#include "window.h"

#define	DEF_WIDTH	1280	/* default width */
#define	DEF_HEIGHT	720	/* default height */

static void
key_q(void)
{
	exit(0);
}

static void
usage(const char *arg0)
{
	note("Usage: %s [-w <width>] [-h <height>] [-A] [-a] [-B] [-C] "
	    "[-D <areas>] [-F] [-f <file>] [-K <keys>] [-k] [-L] "
	    "[-r <radius>] [-R <radius>] [-s <seconds>] [-S <scale>] [-v] "
	    "[-x <random seed>]\n\n", arg0);

	note("\t-w <width>\tMake the display window <width> pixels wide.\n");
	note("\t-h <height>\tMake the display window <height> pixels tall.\n");
	note("\t-A\t\tDisable autopilot mode.\n");
	note("\t-a\t\tDisable animation.\n");
	note("\t-B\t\tRun box blur performance test.\n");
	note("\t-C\t\tDisable the use of a camera.\n");
	note("\t-D <areas>\tEnable debugging output for <areas>.\n");
	note("\t-F\t\tDisable fullscreen mode.\n");
	note("\t-f <file>\tLoad the PPM file <file> as the starting image.\n");
	note("\t-K <keys>\tEnter some keystrokes from command line.\n");
	note("\t-k\t\tRun in keypad mode.\n");
	note("\t-L\t\tLog all keypresses.\n");
	note("\t-r <radius>\tMinimum radius for box blur performance test.\n");
	note("\t-R <radius>\tMaximum radius for box blur performance test.\n");
	note("\t-S <scale>\tCalculate images at <scale> magnification.\n");
	note("\t-s <seconds>\tSave an image every <seconds> seconds.\n");
	note("\t-v\t\tEnable verbose status output.\n");
	note("\t-x <seed>\tSpecify a seed for the random number generator.\n");

	exit(1);
}

int
main(int argc, char **argv)
{
	int		ch;
	pix_t		w, h;
	bool		go_fullscreen;
	bool		enable_autopilot;
	bool		animated;
	bool		boxtest;
	bool		graphics;
	bool		use_keypad;
	char		*keys;
	bool		log_keys;
	pix_t		boxtest_minradius;
	pix_t		boxtest_maxradius;
	time_t		saveperiod;
	float		scale;
	long		randomseed;

	w = DEF_WIDTH;
	h = DEF_HEIGHT;
	go_fullscreen = true;
	enable_autopilot = true;
	animated = true;
	boxtest = false;
	graphics = true;
	use_keypad = false;
	keys = NULL;
	log_keys = false;
	boxtest_minradius = 0;
	boxtest_maxradius = 0;
	saveperiod = 0;
	scale = 1;
	randomseed = getpid();

	while ((ch = getopt(argc, argv,
	    "AaBCD:Ff:Gh:K:kLR:r:S:s:vw:x:?")) != -1) {
		switch (ch) {
		case 'A':
			enable_autopilot = false;
			break;
		case 'a':
			animated = false;
			break;
		case 'B':
			animated = false;
			enable_autopilot = false;
			go_fullscreen = false;
			boxtest = true;
			break;
		case 'C':
			camera_disable();
			break;
		case 'D':
			debug_init_areas(optarg);
			break;
		case 'F':
			go_fullscreen = false;
			break;
		case 'f':
			if (ppm_read_sizes(optarg, &w, &h)) {
				verbose(DB_IMAGE,
				    "Using %s as starting image\n", optarg);
				image_datafile(optarg);
				go_fullscreen = false;
			} else if (camera_try_file(optarg, &w, &h)) {
				verbose(DB_CAMERA,
				    "Using %s as input image stream\n", optarg);
				camera_set_filename(optarg);
				go_fullscreen = false;
			}
			break;
		case 'G':
			graphics = false;
			break;
		case 'h':
			h = atoi(optarg);
			go_fullscreen = false;
			break;
		case 'k':
			use_keypad = true;
			break;
		case 'K':
			keys = optarg;
			break;
		case 'L':
			log_keys = true;
			break;
		case 'r':
			boxtest_minradius = atoi(optarg);
			break;
		case 'R':
			boxtest_maxradius = atoi(optarg);
			break;
		case 's':
			saveperiod = atoi(optarg);
			break;
		case 'S':
			scale = strtof(optarg, NULL);
			break;
		case 'v':
			debug_set_verbose();
			break;
		case 'w':
			w = atoi(optarg);
			go_fullscreen = false;
			break;
		case 'x':
			randomseed = atoi(optarg);
			break;
		case '?':
		default:
			usage(argv[0]);
			break;
		}
	}

	srandbj(randomseed);

	if ((boxtest_minradius != 0 || boxtest_maxradius != 0) && !boxtest) {
		warn("need to use \"-B\" to enable box test\n");
	}

	if (enable_autopilot) {
		autopilot_enable();
	}

	window_set_animated(animated);
	window_set_graphics(graphics);

	if (scale == 0) {
		usage(argv[0]);
	} else {
		window_setscale(scale);
	}

	if (saveperiod != 0) {
		window_saveperiod(saveperiod);
	}

	/*
	 * This initializes GLUT, so it has to come before any of the other
	 * module preinit/init routines.
	 */
	window_create(w, h);

	/*
	 * Initialize all of the subsystems.
	 */
	module_preinit();
	module_init();
	atexit(module_postfini);
	atexit(module_fini);

	/*
	 * Register a handler for quitting the program.
	 *
	 * The keypad itself doesn't have a "q", but I want to be able to
	 * exit the program if I plug in a regular keyboard.
	 */
	key_register('q', KB_DEFAULT, "quit", key_q);
	key_register('q', KB_KEYPAD, "quit", key_q);

	key_set_binding(use_keypad ? KB_KEYPAD : KB_DEFAULT);
	if (log_keys) {
		keylog_start();
	}

	if (go_fullscreen) {
		window_fullscreen();
	}

	/*
	 * Process any other key-based commands before we get moving.
	 */
	if (keys != NULL) {
		key_process(keys);
	}

	if (boxtest) {
		if (boxtest_minradius == 0) {
			boxtest_minradius = 1;
		}
		if (boxtest_maxradius == 0 || boxtest_maxradius > MAX_RADIUS) {
			boxtest_maxradius = MAX_RADIUS;
		}
		box_test(boxtest_minradius, boxtest_maxradius);
	} else {
		window_mainloop();
	}

	return (0);
}
