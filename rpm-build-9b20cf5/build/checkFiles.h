#ifndef CHECKFILES_H
#define CHECKFILES_H

/* Perform filst list check. */
int checkFiles(Spec spec);

/* Helper function: process files with the same name. */
void fiIntersect(const TFI_t fi1, const TFI_t fi2,
		 void (*cb)(const TFI_t fi1, const TFI_t fi2,
			    const char *f, int i1, int i2, void *data),
		 void *data);

#endif
