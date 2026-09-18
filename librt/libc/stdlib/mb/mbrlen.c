#include <internal/_tls.h>
#include <wchar.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

size_t
mbrlen(const char *__restrict s, size_t n, mbstate_t *__restrict ps) {
#ifdef _MB_CAPABLE
  if (ps == NULL) {
      ps = &(__tls_current()->mbst);
    }
#endif
  return mbrtowc(NULL, s, n, ps);
}
