/*
 * The opencl code uses this source file to pull in everything.
 */

/*
 * These files are used by multiple kernels, and must come first.
 */
#include "vectypes.h"
#include "clcommon.h"

/*
 * The common kernel code.
 */
#include "box.cl"
#include "subblock.cl"
#include "color.cl"	// used by heatmap.cl
#include "heatmap.cl"
#include "interp.cl"
#include "reduce.cl"
#include "stroke.cl"

/*
 * The core algorithm comes last; that way, any #define's that it generates
 * don't affect anything else.
 *
 * Its #include lines are added by the "make-kernelsrc" script.
 */
