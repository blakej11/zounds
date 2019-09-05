/*
 * vectypes.h - definitions of boxvector and datavec.
 */

#ifndef	_VECTYPES_H
#define	_VECTYPES_H

#include "vecsizes.h"

/*
 * BOX_DIMENSIONS doesn't need to be defined if the core algorithm isn't
 * using the box blur. But DATA_DIMENSIONS must always be defined.
 */
#ifndef	BOX_DIMENSIONS
#define	BOX_DIMENSIONS	1
#endif
#ifndef	DATA_DIMENSIONS
#error	DATA_DIMENSIONS must be defined.
#endif

/*
 * The data types used in the multiscale algorithm.
 */
#ifdef	__OPENCL_VERSION__		/* OpenCL types */

#if	BOX_DIMENSIONS == 1
typedef float			boxvector;
#elif	BOX_DIMENSIONS == 2
typedef float2			boxvector;
#elif	BOX_DIMENSIONS == 3
typedef float3			boxvector;
#elif	BOX_DIMENSIONS == 4
typedef float4			boxvector;
#else
#error	Do not know how to deal with that value of BOX_DIMENSIONS.
#endif

#if	DATA_DIMENSIONS == 1
typedef float			datavec;
#define	as_datavec(v)		((v).x)
#define	pack_float4(v)		((float4)(v))
#elif	DATA_DIMENSIONS == 3
typedef float3			datavec;
#define	as_datavec(v)		as_float3(v)
#define	pack_float4(v)		((float4)(v, 0))
#elif	DATA_DIMENSIONS == 4
typedef float4			datavec;
#define	as_datavec(v)		as_float4(v)
#define	pack_float4(v)		((float4)(v))
#else
#error	Do not know how to deal with that value of DATA_DIMENSIONS.
#endif

#else					/* C types */

#if	BOX_DIMENSIONS == 1
typedef struct { float s[1]; }	cl_boxvector;
#elif	BOX_DIMENSIONS == 2
typedef cl_float2		cl_boxvector;
#elif	BOX_DIMENSIONS == 3
typedef cl_float3		cl_boxvector;
#elif	BOX_DIMENSIONS == 4
typedef cl_float4		cl_boxvector;
#else
#error	Do not know how to deal with that value of BOX_DIMENSIONS.
#endif

#if	DATA_DIMENSIONS == 1
typedef struct { float s[1]; }	cl_datavec;
#elif	DATA_DIMENSIONS == 3
typedef cl_float3		cl_datavec;
#elif	DATA_DIMENSIONS == 4
typedef cl_float4		cl_datavec;
#else
#error	Do not know how to deal with that value of DATA_DIMENSIONS.
#endif

/* Whether datavec's fit into a sphere or a cube. */
typedef enum {
	DATAVEC_SHAPE_SPHERE,
	DATAVEC_SHAPE_CUBE
} datavec_shape_t;

#endif	/* __OPENCL_VERSION__ */

#endif	/* _VECTYPES_H */
