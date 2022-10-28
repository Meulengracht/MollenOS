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

#ifndef __OS_TYPES_SYSCALL_H__
#define	__OS_TYPES_SYSCALL_H__

#include <os/osdefs.h>
#include <os/usched/cond.h>

/**
 * @brief The system call context is neccessary for system calls that want to support
 * asynchronous calls. We implement this to support green threads in userspace, where it's
 * unwanted to ever block. Instead the system call should keep running and allow the thread
 * to return immediately to userspace, and instead be signalled once the result is ready.
 */
typedef struct OSSyscallContext {
    struct usched_mtx Mutex;
    struct usched_cnd Condition;
    oserr_t           ErrorCode;
} OSSyscallContext_t;

static inline void OSSyscallContextInitialize(OSSyscallContext_t* context) {
    usched_cnd_init(&context->Condition);
    usched_mtx_init(&context->Mutex);
    context->ErrorCode = OS_ESCSTARTED;
}

#endif //!__OS_TYPES_SYSCALL_H__
