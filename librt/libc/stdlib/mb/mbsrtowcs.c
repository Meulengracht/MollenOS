#include <wchar.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

size_t mbsrtowcs(
	wchar_t *__restrict dst,
	const char **__restrict src,
	size_t len,
	mbstate_t *__restrict ps)
{
  return mbsnrtowcs(dst, src, (size_t) -1, len, ps);
}
