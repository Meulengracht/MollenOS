#include <stdio.h>
#include <stdarg.h>

int
_vscprintf(
   const char *format,
   va_list argptr)
{
    int ret;
    FILE* nulfile = fopen("nul", "w");
    if(nulfile == NULL)
    {
        /* This should never happen... */
        return -1;
    }
    ret = streamout(nulfile, format, argptr);
    fclose(nulfile);
    return ret;
}
