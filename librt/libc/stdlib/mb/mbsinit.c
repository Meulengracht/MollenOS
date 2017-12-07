#include <wchar.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

int
mbsinit(
    _In_ const mbstate_t *ps)
{
    if (ps == NULL || ps->__count == 0)
        return 1;
    else
        return 0;
}
