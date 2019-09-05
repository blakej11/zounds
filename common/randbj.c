/*
 * My own implementation of drand48() / lrand48() / srand48(),
 * to ensure the same results on all platforms.
 */
#include "randbj.h"

static const unsigned long long	A = 0x5deece66d;
static const unsigned long long	C = 0xb;
static unsigned long long	RV = 0x1234abcd330e;

long
lrandbj(void)
{
	RV = (A * RV + C) & ((1ULL << 48) - 1);
	return ((long)(RV >> 17));
}

double
drandbj(void)
{
	unsigned long long	nrv;
	int			sc;
	double			r;

	RV = (A * RV + C) & ((1ULL << 48) - 1);

	for (nrv = RV << 4, sc = 0;
	    (nrv & (1ULL << 52)) == 0;
	    nrv <<= 1, sc++)
		;
	*(long long *)&r = ((0x3feULL ^ sc) << 52) ^ nrv;

	return (r);
}

void
srandbj(int r)
{
	RV = (((unsigned long long)r) << 16) | 0x330e;
}
