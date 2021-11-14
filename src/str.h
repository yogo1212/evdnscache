#ifndef _STR_H
#define _STR_H

#include <string.h>


/**
 * copy at most len bytes from src to dest using strncpy.
 * return false if truncated, true otherwise
 */
#define copy_string(dest, src, dest_len) \
	__extension__ \
	({ \
		size_t len = strlen(src); \
		bool trunc = len > (dest_len) - 1; \
		if (trunc) \
			len = (dest_len) - 1; \
		memcpy(dest, src, len); \
		(dest)[len] = '\0'; \
		! trunc; \
	})


#define strncata(src, len) \
	__extension__ \
	({ \
		char *target = NULL; \
		target = alloca(len + 1); \
		strncat(target, src, len); \
		target[len] = '\0'; \
		target; \
	})

#endif
