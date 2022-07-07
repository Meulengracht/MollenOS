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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Generic IO Events. These are functions that may be supported by all
 * io descriptors. If they are not supported errno is set to ENOTSUPP
 */

//#define __TRACE

#include <ddk/handle.h>
#include <ddk/utils.h>
#include <errno.h>
#include <internal/_io.h>
#include <internal/_ioset.h>
#include <ioctl.h>
#include <ioset.h>
#include <os/mollenos.h>
#include <stdlib.h>

static void                ioset_append(stdio_handle_t*, struct ioset_entry*);
static struct ioset_entry* ioset_get(stdio_handle_t*, int);
static struct ioset_entry* ioset_remove(stdio_handle_t*, int);

int ioset(int flags)
{
    stdio_handle_t* ioObject;
    oscode_t      osStatus;
    uuid_t          handle;
    int             status;
    
    status = stdio_handle_create(-1, WX_OPEN | WX_DONTINHERIT, &ioObject);
    if (status) {
        return -1;
    }
    
    osStatus = notification_queue_create(0, &handle);
    if (osStatus != OsOK) {
        (void)OsCodeToErrNo(osStatus);
        stdio_handle_destroy(ioObject, 0);
        return -1;
    }
    
    stdio_handle_set_handle(ioObject, handle);
    stdio_handle_set_ops_type(ioObject, STDIO_HANDLE_SET);
    
    ioObject->object.data.ioset.entries = NULL;
    
    return ioObject->fd;
}

int ioset_ctrl(int evt_iod, int op, int iod, struct ioset_event* event)
{
    stdio_handle_t*     setObject = stdio_handle_get(evt_iod);
    stdio_handle_t*     ioObject  = stdio_handle_get(iod);
    oscode_t          status;
    struct ioset_entry * entry;
    
    if (!event) {
        _set_errno(EINVAL);
        return -1;
    }
    
    if (!setObject || !ioObject) {
        _set_errno(EBADF);
        return -1;
    }
    
    if (op == IOSET_ADD) {
        entry = malloc(sizeof(struct ioset_entry));
        if (!entry) {
            _set_errno(ENOMEM);
            return -1;
        }
        
        entry->iod          = iod;
        entry->event.events = event->events;
        entry->event.data   = event->data;
        entry->link         = NULL;
        ioset_append(setObject, entry);
    }
    else if (op == IOSET_MOD) {
        entry = ioset_get(setObject, iod);
        if (!entry) {
            _set_errno(ENOENT);
            return -1;
        }
        entry->event.events = event->events;
    }
    else if (op == IOSET_DEL) {
        entry = ioset_remove(setObject, iod);
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
    
    status = notification_queue_ctrl(setObject->object.handle, op,
                                     ioObject->object.handle, event);
    if (status != OsOK) {
        (void)OsCodeToErrNo(status);
        return -1;
    }
    return 0;
}

int ioset_wait(int set_iod, struct ioset_event* events, int max_events, int timeout)
{
    stdio_handle_t*     setObject = stdio_handle_get(set_iod);
    int                 numEvents;
    struct ioset_entry* entry;
    oscode_t          status;
    int                 i = 0;
    
    TRACE("[ioset] [wait] %i, %i", set_iod, max_events);
    
    if (!setObject) {
        _set_errno(EBADF);
        return -1;
    }
    
    entry = setObject->object.data.ioset.entries;
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
    
    status = notification_queue_wait(setObject->object.handle, &events[0],
                                     max_events, i, timeout, &numEvents);
    if (status != OsOK) {
        (void)OsCodeToErrNo(status);
        return -1;
    }
    return numEvents;
}

static void ioset_append(stdio_handle_t* handle, struct ioset_entry* entry)
{
    struct ioset_entry * itr = handle->object.data.ioset.entries;
    
    if (!handle->object.data.ioset.entries) {
        handle->object.data.ioset.entries = entry;
    }
    else {
        while (itr->link) {
            itr = itr->link;
        }
        itr->link = entry;
    }
}

static struct ioset_entry* ioset_get(stdio_handle_t* handle, int iod)
{
    struct ioset_entry * itr = handle->object.data.ioset.entries;
    while (itr) {
        if (itr->iod == iod) {
            return itr;
        }
    }
    return NULL;
}

static struct ioset_entry* ioset_remove(stdio_handle_t* handle, int iod)
{
    struct ioset_entry * itr = handle->object.data.ioset.entries;
    struct ioset_entry * prev;
    if (itr && itr->iod == iod) {
        handle->object.data.ioset.entries = itr->link;
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
