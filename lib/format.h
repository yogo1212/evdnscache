#ifndef _FORMAT_H
#define _FORMAT_H

#include <stdio.h>
#include <stdlib.h>

#define sprintfa(fmt, ...) \
	__extension__ \
	({ \
		int len = snprintf(NULL, 0, fmt, __VA_ARGS__); \
		char *target = NULL; \
		if (len >= 0) { \
			target = alloca(len + 1); \
			sprintf(target, fmt, __VA_ARGS__); \
		} \
		target; \
	})

#define systemf(fmt, ...) \
	__extension__ \
	({ \
		char *tmp = sprintfa(fmt, __VA_ARGS__); \
		int res = -1; \
		if (tmp) \
			res = system(tmp); \
		res; \
	})

#endif
