#ifndef __INTERNAL_IOSET_H__
#define __INTERNAL_IOSET_H__

#include <ioset.h>

struct ioset_entry {
    int                iod;
    struct ioset_event event;
    struct ioset_entry * link;
};

struct ioset {
    struct ioset_entry * entries;
};

#endif //!__INTERNAL_IOSET_H__
