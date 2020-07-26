#ifndef __INTERNAL_EVT_H__
#define __INTERNAL_EVT_H__

#include <os/osdefs.h>

struct evt {
    unsigned int flags;
    unsigned int options;
    size_t       initialValue;
    atomic_int*  sync_address;
};

#endif //!__INTERNAL_EVT_H__
