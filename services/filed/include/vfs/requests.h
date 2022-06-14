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

enum FileSystemRequestState {
    FileSystemRequest_CREATED,
    FileSystemRequest_INPROGRESS,
    FileSystemRequest_ABORTED,
    FileSystemRequest_DONE
};

typedef struct VFSRequest {
    UUId_t                      id;
    enum FileSystemRequestState state;
    UUId_t                      processId;
    struct usched_mtx           lock;
    struct usched_cnd           signal;

    // request parameters
    union {
        struct {
            const char*  path;
            unsigned int options;
            unsigned int access;
        } open;
        struct {
            UUId_t fileHandle;
        } close;
        struct {
            const char*  path;
            unsigned int options;
        } delete_path;
        struct {
            UUId_t fileHandle;
            UUId_t bufferHandle;
            size_t offset;
            size_t length;
        } transfer;
        struct {
            UUId_t   fileHandle;
            uint32_t position_low;
            uint32_t position_high;
            UUId_t   bufferHandle;
            size_t   offset;
            size_t   length;
        } transfer_absolute;
        struct {
            UUId_t   fileHandle;
            uint32_t position_low;
            uint32_t position_high;
        } seek;
        struct {
            UUId_t fileHandle;
        } flush;
        struct {
            const char* from;
            const char* to;
            int         copy;
        } move;
        struct {
            const char* from;
            const char* to;
            int         symbolic;
        } link;
        struct {
            UUId_t fileHandle;
        } get_position;
        struct {
            UUId_t fileHandle;
        } get_options;
        struct {
            UUId_t       fileHandle;
            unsigned int options;
            unsigned int access;
        } set_options;
        struct {
            UUId_t fileHandle;
        } get_size;
        struct {
            UUId_t   fileHandle;
            uint32_t size_low;
            uint32_t size_high;
        } set_size;
        struct {
            UUId_t fileHandle;
        } stat_handle;
        struct {
            const char* path;
            int         follow_links;
        } stat_path;
    } parameters;

    // must be last member of struct
    struct gracht_message message[];
} FileSystemRequest_t;

/**
 * @brief Cleans up any resources allocated for the request
 *
 * @param request A pointer to the request that should be destroyed.
 */
extern void VfsRequestDestroy(FileSystemRequest_t* request);

/**
 * @brief Updates the state for a request currently running
 *
 * @param request A pointer to the request that should be changed.
 * @param state   The state that the request should switch to.
 */
extern void VfsRequestSetState(FileSystemRequest_t* request, enum FileSystemRequestState state);

#endif //!__VFS_REQUESTS_H__
