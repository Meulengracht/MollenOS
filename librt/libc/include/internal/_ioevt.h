#ifndef __INTERNAL_IOEVT_H__
#define __INTERNAL_IOEVT_H__

#include <ioevt.h>

struct ioevt_entry {
    int                 iod;
    struct ioevt_event  event;
    struct ioevt_entry* link;
};

struct ioevt {
    struct ioevt_entry* entries;
};

#endif //!__INTERNAL_IOEVT_H__
