#include <sys/types.h>

/* Copies src to dst.  Unlike strncpy, copies at most size-1 plus the NULL-term.
 * Returns strlen(src). */
size_t strlcpy(char *dst, const char *src, size_t size) {
	size_t len = 0;

	if(!size)
		return strlen(src);

	while(1) {
		if(len == size) {
			dst[-1] = 0;
			return len + strlen(src);
		}
		if(!*src) {
			*dst = 0;
			len++;
			return len;
		}
		*dst++ = *src++;
		len++;
	}
}
