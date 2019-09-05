#ifndef	_TWEAK_H
#define	_TWEAK_H

#include "types.h"

extern void	tweak_preinit(void);
extern void	tweak_init(void);
extern void	tweak_fini(void);

extern int	tweak_nscales(void);
extern int	tweak_nbox(void);
extern int	tweak_rendertype(void);
extern pix_t	tweak_box_radius(int scale);
extern float	tweak_multiscale_adj(int scale);

/* callback used by the tweak code */
extern void	multiscale_adjust(void);

/*
 * Maximum values for some of the parameters.
 */
#define	NSCALES		9	/* number of scales to operate on */
#define	NADJTYPE	7	/* number of adjustment arrays */

#endif	/* _TWEAK_H */
