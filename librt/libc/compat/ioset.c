/**
 * Copyright 2023, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ? , either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

//#define __TRACE

#include "os/notification_queue.h"
#include <ddk/utils.h>
#include <errno.h>
#include <internal/_io.h>
#include <internal/_tls.h>
#include <io.h>
#include <ioctl.h>
#include <ioset.h>
#include <os/mollenos.h>
#include <os/handle.h>
#include <stdlib.h>

struct IOSetEntry {
    int                iod;
    struct ioset_event event;
    struct IOSetEntry* link;
};

struct IOSet {
    struct IOSetEntry* entries;
};

static void               ioset_append(struct IOSet*, struct IOSetEntry*);
static struct IOSetEntry* ioset_get(struct IOSet*, int);
static struct IOSetEntry* ioset_remove(struct IOSet*, int);

static void __ioset_close(stdio_handle_t*, int);

stdio_ops_t g_iosetOps = {
        .close = __ioset_close
};

static struct IOSet*
__ioset_new(void)
{
    struct IOSet* ios;

    ios = malloc(sizeof(struct IOSet));
    if (ios == NULL) {
        return NULL;
    }
    ios->entries = NULL;
    return ios;
}

static void
__ioset_delete(
        _In_ struct IOSet* ios)
{
    struct IOSetEntry* i;
    if (ios == NULL) {
        return;
    }

    i = ios->entries;
    while (i) {
        void* tmp = i->link;
        free(i);
        i = tmp;
    }
    free(ios);
}

int ioset(int flags)
{
    stdio_handle_t* object;
    struct IOSet*   ios;
    oserr_t         oserr;
    OSHandle_t      handle;
    int             status;

    ios = __ioset_new();
    if (ios == NULL) {
        return -1;
    }

    oserr = OSNotificationQueueCreate(0, &handle);
    if (oserr != OS_EOK) {
        free(ios);
        (void)OsErrToErrNo(oserr);
        return -1;
    }

    status = stdio_handle_create(
            -1,
            flags | O_NOINHERIT,
            0,
            IOSET_SIGNATURE,
            ios,
            &object
    );
    if (status) {
        OSHandleDestroy(&handle);
        free(ios);
        return -1;
    }

    stdio_handle_set_handle(object, &handle);
    return stdio_handle_iod(object);
}

int ioset_ctrl(int evt_iod, int op, int iod, struct ioset_event* event)
{
    stdio_handle_t*    setObject = stdio_handle_get(evt_iod);
    stdio_handle_t*    ioObject  = stdio_handle_get(iod);
    struct IOSet*      ios;
    struct IOSetEntry* entry;
    
    if (!event) {
        _set_errno(EINVAL);
        return -1;
    }
    
    if (!setObject || stdio_handle_signature(setObject) != IOSET_SIGNATURE || !ioObject) {
        _set_errno(EBADF);
        return -1;
    }

    ios = setObject->OpsContext;
    if (op == IOSET_ADD) {
        entry = malloc(sizeof(struct IOSetEntry));
        if (!entry) {
            _set_errno(ENOMEM);
            return -1;
        }
        
        entry->iod          = iod;
        entry->event.events = event->events;
        entry->event.data   = event->data;
        entry->link         = NULL;
        ioset_append(ios, entry);
    } else if (op == IOSET_MOD) {
        entry = ioset_get(ios, iod);
        if (!entry) {
            _set_errno(ENOENT);
            return -1;
        }
        entry->event.events = event->events;
    } else if (op == IOSET_DEL) {
        entry = ioset_remove(ios, iod);
        if (!entry) {
            _set_errno(ENOENT);
            return -1;
        }
        free(entry);
    } else {
        _set_errno(EINVAL);
        return -1;
    }

    return OsErrToErrNo(
            OSNotificationQueueCtrl(
                    &setObject->OSHandle,
                    op,
                    &ioObject->OSHandle,
                    event
            )
    );
}

int ioset_wait(int set_iod, struct ioset_event* events, int max_events, const struct timespec* until)
{
    stdio_handle_t*    setObject = stdio_handle_get(set_iod);
    int                numEvents;
    struct IOSet*      ios;
    struct IOSetEntry* entry;
    oserr_t            oserr;
    int                i = 0;
    OSAsyncContext_t*  asyncContext = __tls_current()->async_context;
    
    TRACE("[ioset] [wait] %i, %i", set_iod, max_events);
    
    if (!setObject || stdio_handle_signature(setObject) != IOSET_SIGNATURE) {
        _set_errno(EBADF);
        return -1;
    }

    ios = setObject->OpsContext;
    entry = ios->entries;
    while (entry && i < max_events) {
        TRACE("[ioset] [wait] %i = 0x%x", entry->iod, entry->event.events);
        if ((entry->event.events & (IOSETIN | IOSETLVT)) == (IOSETIN | IOSETLVT)) {
            int bytesAvailable = 0;
            int result         = ioctl(entry->iod, FIONREAD, &bytesAvailable);
            TRACE("[ioset] [wait] %i = %i bytes available [%i]", entry->iod, bytesAvailable, result);
            if (!result && bytesAvailable) {
                events[i].events = IOSETIN;
                events[i].data   = entry->event.data;
                i++;
            }
        }
        entry = entry->link;
    }

    if (asyncContext) {
        OSAsyncContextInitialize(asyncContext);
    }
    oserr = OSNotificationQueueWait(
            &setObject->OSHandle,
            &events[0],
            max_events,
            i,
            until == NULL ? NULL : &(OSTimestamp_t) {
                .Seconds = until->tv_sec, .Nanoseconds = until->tv_nsec
            },
            &numEvents,
            asyncContext
    );
    if (oserr != OS_EOK) {
        return OsErrToErrNo(oserr);
    }
    return numEvents;
}

static void
ioset_append(struct IOSet* ios, struct IOSetEntry* entry)
{
    struct IOSetEntry* itr = ios->entries;
    
    if (!ios->entries) {
        ios->entries = entry;
    } else {
        while (itr->link) {
            itr = itr->link;
        }
        itr->link = entry;
    }
}

static struct IOSetEntry*
ioset_get(struct IOSet* ios, int iod)
{
    struct IOSetEntry* itr = ios->entries;
    while (itr) {
        if (itr->iod == iod) {
            return itr;
        }
    }
    return NULL;
}

static struct IOSetEntry*
ioset_remove(struct IOSet* ios, int iod)
{
    struct IOSetEntry* itr = ios->entries;
    struct IOSetEntry* prev;
    if (itr && itr->iod == iod) {
        ios->entries = itr->link;
        return itr;
    }
    
    while (itr) {
        if (itr->iod == iod) {
            prev->link = itr->link;
            return itr;
        }
        
        // prev has no value initially, but this is ok as we check the
        // first element seperately
        prev = itr;
        itr  = itr->link;
    }
    return NULL;
}

static void __ioset_close(stdio_handle_t* handle, int options)
{
    if (options & STDIO_CLOSE_FULL) {
        __ioset_delete(handle->OpsContext);
    }
}
