/*
 * template.h - file name templates.
 */

#ifndef	_TEMPLATE_H
#define	_TEMPLATE_H

typedef struct template template_t;

/*
 * Create a template for saving image files in the specified directory.
 */
extern template_t *
template_alloc(char *dirname);

/*
 * Generate a full pathname for saving an image file. The optional label
 * "label" describes what type of image it is, and "steps" is the number
 * of steps taken so far which led to this iamge.
 */
extern char *
template_name(template_t *t, const char *label, int steps);

#endif	/* _TEMPLATE_H */

