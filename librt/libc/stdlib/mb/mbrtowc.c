#include "../../threads/tss.h"
#include <wchar.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include "../local.h"

size_t _mbrtowc_r(
	wchar_t *pwc,
	const char *s,
	size_t n,
	mbstate_t *ps)
{
  int retval = 0;

#ifdef _MB_CAPABLE
  if (ps == NULL) {
      ps = &(tls_current()->mbst);
    }
#endif

  if (s == NULL)
    retval = __MBTOWC (NULL, "", 1, ps);
  else
    retval = __MBTOWC (pwc, s, n, ps);

  if (retval == -1)
    {
      ps->__count = 0;
      _set_errno(EILSEQ);
      return (size_t)(-1);
    }
  else
    return (size_t)retval;
}

size_t mbrtowc(
	wchar_t *__restrict pwc,
	const char *__restrict s,
	size_t n,
	mbstate_t *__restrict ps)
{
#if defined(PREFER_SIZE_OVER_SPEED) || defined(__OPTIMIZE_SIZE__)
  return _mbrtowc_r (_REENT, pwc, s, n, ps);
#else
  int retval = 0;

#ifdef _MB_CAPABLE
  if (ps == NULL)
    {
      ps = &(tls_current()->mbst);
    }
#endif

  if (s == NULL)
    retval = __MBTOWC (NULL, "", 1, ps);
  else
    retval = __MBTOWC (pwc, s, n, ps);

  if (retval == -1)
    {
      ps->__count = 0;
      _set_errno(EILSEQ);
      return (size_t)(-1);
    }
  else
    return (size_t)retval;
#endif /* not PREFER_SIZE_OVER_SPEED */
}
