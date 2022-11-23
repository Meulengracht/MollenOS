/* Doc in wcsnrtombs.c */
#include <wchar.h>
#include <stdlib.h>

size_t wcsrtombs(
	char *__restrict dst,
	const wchar_t **__restrict src,
	size_t len,
	mbstate_t *__restrict ps)
{
		return wcsnrtombs(dst, src, (size_t) -1, len, ps);
}
