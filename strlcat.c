#include <sys/types.h>
#include <string.h>

/* Appends src to dst.  Unlike strncat, copies at most size-strlen(src)-1 plus
 * the NULL-term.  Returns strlen(src) + strlen(dst) + 1. */
size_t strlcat(char *dst, const char *src, size_t size) {
	size_t len = 0;

	len = strlen(dst);
	dst += len;
	if(len >= size)
		return len + strlen(src) + 1;

	return len + strlcpy(dst, src, size - len);
}
