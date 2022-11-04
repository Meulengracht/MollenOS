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
 */

#ifndef __FS_REQUESTS_H__
#define __FS_REQUESTS_H__

#include <gracht/link/link.h>
#include <os/usched/mutex.h>
#include <os/usched/cond.h>
#include <ds/mstring.h>
#include <ddk/filesystem.h>

typedef struct VFSRequest {
    uuid_t            id;
    struct usched_mtx lock;
    struct usched_cnd signal;

    // request parameters
    union {
        struct VFSStorageParameters init;
        struct {
            void* context;
        } destroy;
        struct {
            void*      context;
            mstring_t* path;
        } open;
        struct {
            void*      fscontext;
            void*      fcontext;
            mstring_t* name;
            uint32_t   owner;
            uint32_t   flags;
            uint32_t   permissions;
        } create;
        struct {
            void* fscontext;
            void* fcontext;
        } close;
        struct {
            void* fscontext;
        } stat;
        struct {
            void*      fscontext;
            void*      fcontext;
            mstring_t* name;
            mstring_t* target;
            int        symbolic;
        } link;
        struct {
            void*      fscontext;
            mstring_t* path;
        } unlink;
        struct {
            void*      fscontext;
            mstring_t* path;
        } readlink;
        struct {
            void*      fscontext;
            mstring_t* from;
            mstring_t* to;
            int        copy;
        } move;
        struct {
            void*    fscontext;
            void*    fcontext;
            uuid_t   buffer_id;
            size_t   offset;
            uint64_t count;
        } transfer;
        struct {
            void*    fscontext;
            void*    fcontext;
            uint64_t size;
        } truncate;
        struct {
            void*    fscontext;
            void*    fcontext;
            uint64_t position;
        } seek;
    } parameters;

    // must be last member of struct
    struct gracht_message message[];
} FileSystemRequest_t;

/**
 * @brief Cleans up any resources allocated for the request
 *
 * @param request A pointer to the request that should be destroyed.
 */
extern void FSRequestDestroy(FileSystemRequest_t* request);

#endif //!__FS_REQUESTS_H__
