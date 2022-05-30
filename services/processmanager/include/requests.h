/**
 * Copyright 2021, Philip Meulengracht
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
 * Virtual File Request Definitions & Structures
 * - This header describes the base virtual file-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
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
            const char*            path;
            const char*            args;
            const void*            inherit;
            ProcessConfiguration_t conf;
        } spawn;
        struct {
            UUId_t threadHandle;
            UUId_t bufferHandle;
            size_t bufferOffset;
        } get_initblock;
        struct {
            UUId_t       handle;
            unsigned int timeout;
        } join;
        struct {
            UUId_t handle;
            int    exit_code;
        } terminate;
        struct {
            UUId_t killer_handle;
            UUId_t victim_handle;
            int    signal;
        } signal;
        struct {
            UUId_t      handle;
            const char* path;
        } load_library;
        struct {
            UUId_t   handle;
            Handle_t library_handle;
            const char* name;
        } get_function;
        struct {
            UUId_t   handle;
            Handle_t library_handle;
        } unload_library;
        struct {
            UUId_t handle;
        } stat_handle;
        struct {
            UUId_t      handle;
            const char* path;
        } set_cwd;
        struct {
            UUId_t           thread_handle;
            UUId_t           process_handle;
            const Context_t* context;
            int              reason;
        } crash;
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
