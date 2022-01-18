/**
 * Copyright 2022, Philip Meulengracht
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
 * Device Manager
 * - Implementation of the device manager in the operating system.
 *   Keeps track of devices, their loaded drivers and bus management.
 */

#ifndef __VFS_REQUESTS_H__
#define __VFS_REQUESTS_H__

#include <gracht/link/link.h>
#include <os/usched/mutex.h>
#include <os/usched/cond.h>
#include <ds/mstring.h>
#include <ds/list.h>
#include <os/types/process.h>
#include <os/context.h>

enum RequestState {
    RequestState_CREATED,
    RequestState_INPROGRESS,
    RequestState_ABORTED,
    RequestState_DONE
};

typedef struct Request {
    UUId_t             id;
    enum RequestState  state;
    struct usched_cnd  signal;
    element_t          leaf;

    // request parameters
    union {
        struct {
            UUId_t driver_id;
            UUId_t driver_handle;
        } notify;
        struct {
            uint8_t*     device_buffer;
            uint32_t     buffer_size;
            unsigned int flags;
        } create;
        struct {
            UUId_t device_id;
        } destroy;
        struct {
            uint8_t protocol;
        } get_devices_by_protocol;
        struct {
            UUId_t      device_id;
            uint8_t     protocol_id;
            const char* protocol_name;
        } register_protocol;

        struct {
            UUId_t       device_id;
            unsigned int command;
            unsigned int flags;
        } ioctl;
        struct {
            UUId_t       device_id;
            int          direction;
            unsigned int command;
            size_t       value;
            unsigned int width;
        } ioctl2;
    } parameters;

    // must be last member of struct
    struct gracht_message message[];
} Request_t;

/**
 * @brief Cleans up any resources allocated for the request
 *
 * @param request A pointer to the request that should be destroyed.
 */
extern void RequestDestroy(Request_t* request);

/**
 * @brief Updates the state for a request currently running
 *
 * @param request A pointer to the request that should be changed.
 * @param state   The state that the request should switch to.
 */
extern void RequestSetState(Request_t* request, enum RequestState state);

#endif //!__VFS_REQUESTS_H__
