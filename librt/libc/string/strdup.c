/*
 *     STRING
 *     Creation
 */

#include <string.h>

#ifdef LIBC_KERNEL
#include <heap.h>
#define malloc kmalloc
#else
#include <stdlib.h>
#endif

char *strdup(const char *str)
{
    size_t len;
    char*  out;
    if (!str) {
        return NULL;
    }

    len = strlen(str);
    out = (char*)malloc(sizeof(char) * (len+1));
    memcpy(out, str, len+1);
    return out;
}

char *strndup(const char *str, size_t len)
{
    char* out;
    if (!str) {
        return NULL;
    }
    
    out = (char*)malloc(sizeof(char) * (len + 1));
    memcpy(out, str, len);
    out[len] = '\0';
    return out;
}
