#include <stdarg.h>
#include <stdio.h>

__EXTERN
int _vcwprintf(
  _In_ __CONST wchar_t* format, 
  _In_ va_list va);

int
_cwprintf(
  _In_ __CONST wchar_t* format, 
  ...)
{
  int retval;
  va_list valist;

  va_start( valist, format );
  retval = _vcwprintf(format, valist);
  va_end(valist);

  return retval;
}
