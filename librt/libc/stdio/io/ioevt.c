/**
 * MollenOS
 *
 * Copyright 2019, Philip Meulengracht
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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Generic IO Events. These are functions that may be supported by all
 * io descriptors. If they are not supported errno is set to ENOTSUPP
 */

#include <ddk/handle.h>
#include <errno.h>
#include <internal/_io.h>
#include <internal/_ioevt.h>
#include <ioctl.h>
#include <ioevt.h>
#include <os/mollenos.h>
#include <stdlib.h>

static void                ioevt_append(stdio_handle_t*, struct ioevt_entry*);
static struct ioevt_entry* ioevt_get(stdio_handle_t*, int);
static struct ioevt_entry* ioevt_remove(stdio_handle_t*, int);

int ioevt(int flags)
{
    stdio_handle_t* ioObject;
    OsStatus_t      osStatus;
    UUId_t          handle;
    int             status;
    
    status = stdio_handle_create(-1, WX_OPEN | WX_DONTINHERIT, &ioObject);
    if (status) {
        return -1;
    }
    
    osStatus = notification_queue_create(0, &handle);
    if (osStatus != OsSuccess) {
        (void)OsStatusToErrno(osStatus);
        stdio_handle_destroy(ioObject, 0);
        return -1;
    }
    
    stdio_handle_set_handle(ioObject, handle);
    stdio_handle_set_ops_type(ioObject, STDIO_HANDLE_EVT);
    
    ioObject->object.data.ioevt.entries = NULL;
    
    return ioObject->fd;
}

int ioevt_ctrl(int evt_iod, int op, int iod, struct ioevt_event* event)
{
    stdio_handle_t*     evtObject = stdio_handle_get(evt_iod);
    stdio_handle_t*     ioObject  = stdio_handle_get(iod);
    OsStatus_t          status;
    struct ioevt_entry* entry;
    
    if (!event) {
        _set_errno(EINVAL);
        return -1;
    }
    
    if (!evtObject || !ioObject) {
        _set_errno(EBADF);
        return -1;
    }
    
    if (op == IOEVT_ADD) {
        entry = malloc(sizeof(struct ioevt_entry));
        if (!entry) {
            _set_errno(ENOMEM);
            return -1;
        }
        
        entry->iod          = iod;
        entry->event.events = event->events;
        entry->event.data   = event->data;
        entry->link         = NULL;
        ioevt_append(evtObject, entry);
    }
    else if (op == IOEVT_MOD) {
        entry = ioevt_get(evtObject, iod);
        if (!entry) {
            _set_errno(ENOENT);
            return -1;
        }
        entry->event.events = event->events;
    }
    else if (op == IOEVT_DEL) {
        entry = ioevt_remove(evtObject, iod);
        if (!entry) {
            _set_errno(ENOENT);
            return -1;
        }
        free(entry);
    }
    else {
        _set_errno(EINVAL);
        return -1;
    }
    
    status = notification_queue_ctrl(evtObject->object.handle, op,
        ioObject->object.handle, event);
    if (status != OsSuccess) {
        (void)OsStatusToErrno(status);
        return -1;
    }
    return 0;
}

int ioevt_wait(int evt_iod, struct ioevt_event* events, int max_events, int timeout)
{
    stdio_handle_t*     evtObject = stdio_handle_get(evt_iod);
    int                 numEvents;
    struct ioevt_entry* entry;
    OsStatus_t          status;
    int                 i = 0;
    
    if (!evtObject) {
        _set_errno(EBADF);
        return -1;
    }
    
    entry = evtObject->object.data.ioevt.entries;
    while (entry && i < max_events) {
        if ((entry->event.events & (IOEVTIN | IOEVTLVT)) == (IOEVTIN | IOEVTLVT)) {
            int bytesAvailable = 0;
            ioctl(entry->iod, FIONREAD, &bytesAvailable);
            if (bytesAvailable) {
                events[i].events = IOEVTIN;
                events[i].data   = entry->event.data;
                i++;
            }
        }
        entry = entry->link;
    }
    
    status = notification_queue_wait(evtObject->object.handle, &events[0], 
        max_events, i, timeout, &numEvents);
    if (status != OsSuccess) {
        (void)OsStatusToErrno(status);
        return -1;
    }
    return numEvents;
}

static void ioevt_append(stdio_handle_t* handle, struct ioevt_entry* entry)
{
    struct ioevt_entry* itr = handle->object.data.ioevt.entries;
    
    if (!handle->object.data.ioevt.entries) {
        handle->object.data.ioevt.entries = entry;
    }
    else {
        while (itr->link) {
            itr = itr->link;
        }
        itr->link = entry;
    }
}

static struct ioevt_entry* ioevt_get(stdio_handle_t* handle, int iod)
{
    struct ioevt_entry* itr = handle->object.data.ioevt.entries;
    while (itr) {
        if (itr->iod == iod) {
            return itr;
        }
    }
    return NULL;
}

static struct ioevt_entry* ioevt_remove(stdio_handle_t* handle, int iod)
{
    struct ioevt_entry* itr = handle->object.data.ioevt.entries;
    struct ioevt_entry* prev;
    if (itr && itr->iod == iod) {
        handle->object.data.ioevt.entries = itr->link;
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
