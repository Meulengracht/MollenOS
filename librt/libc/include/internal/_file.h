#ifndef __INTERNAL_FILE_H__
#define __INTERNAL_FILE_H__

#include <os/usched/mutex.h>

typedef struct _FILE {
    int               _fd;
    char*             _ptr;
    int               _cnt;
    char*             _base;
    int               _flag;
    int               _charbuf;
    int               _bufsiz;
    char*             _tmpfname;
    struct usched_mtx _lock;
} FILE;

#endif //!__INTERNAL_FILE_H__
