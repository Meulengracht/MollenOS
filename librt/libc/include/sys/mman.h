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

#ifndef __SYS_MMAN_H__
#define __SYS_MMAN_H__

#define __need_size_t
#include <crtdefs.h>
#include <stddef.h>
#include <sys/types.h>

// Protection flags
#define PROT_NONE  0
#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define PROT_EXEC  0x4

// Map flags
#define MAP_SHARED          0x1
#define MAP_SHARED_VALIDATE 0x2
#define MAP_PRIVATE         0x3

// Put the mapping into the first 2 Gigabytes of the process
// address space. This flag is supported only on x86-64, for
// 64-bit programs. It was added to allow thread stacks to
// be allocated somewhere in the first 2 GB of memory, so as
// to improve context-switch performance on some early 64-bit
// processors. Modern x86-64 processors no longer have this
// performance problem, so use of this flag is not required
// on those systems. The MAP_32BIT flag is ignored when
// MAP_FIXED is set.
#define MAP_32BIT 0x4

// The mapping is not backed by any file; its contents are
// initialized to zero. The fd argument is ignored; however,
// some implementations require fd to be -1 if MAP_ANONYMOUS
// (or MAP_ANON) is specified, and portable applications
// should ensure this. The offset argument should be zero.
#define MAP_ANONYMOUS 0x8
#define MAP_ANON      MAP_ANONYMOUS

// Flags here are no-op and only defined for compatability,
// they are not actually supposed to be supported anyway and
// are no-op too on modern linux.
#define MAP_DENYWRITE 0x10
#define MAP_EXECUTABLE 0x20
#define MAP_FILE       0x40

// On Vali, MAP_STACK is aliased to MAP_GROWSDOWN for several reasons, most
// of them due to the fact that userspace applications cannot setup a full
// process on their own, and that a *real* thread-stack is created by the kernel.
// However stacks used in userspace are just normal heap memory as userspace threads
// are green-threads, and not actual kernel threads.
#define MAP_GROWSDOWN 0x200
#define MAP_STACK     MAP_GROWSDOWN

#define MAP_FIXED           0x80
#define MAP_FIXED_NOREPLACE 0x100
#define MAP_HUGETLB         0x400
#define MAP_HUGE_2MB        (MAP_HUGETLB | 0x800)
#define MAP_HUGE_1GB        (MAP_HUGETLB | 0x1000)
#define MAP_LOCKED          0x2000
#define MAP_NONBLOCK        0x4000
#define MAP_NORESERVE       0x8000
#define MAP_POPULATE        0x10000

// Unsupported on Vali
#define MAP_SYNC 0x20000

// Don't clear anonymous pages. This flag is intended to
// improve performance on embedded devices.
#define MAP_UNINITIALIZED 0x40000

// Memory sync flags
#define MS_SYNC       0
#define MS_ASYNC      0x1
#define MS_INVALIDATE 0x2

/**
 * @brief Creates a new mapping in the virtual address space of the
 * calling process. The starting address for the new mapping is
 * specified in addr. The length argument specifies the length of
 * the mapping (which must be greater than 0).
 * @param addr If addr is NULL, then the kernel chooses the (page-aligned)
 *             address at which to create the mapping; this is the most portable
 *             method of creating a new mapping. If addr is not NULL, then the
 *             kernel takes it as a hint about where to place the mapping; on
 *             Linux, the kernel will pick a nearby page boundary and attempt
 *             to create the mapping there. If another mapping already exists
 *             there, the kernel picks a new address that may or may not depend
 *             on the hint. The address of the new mapping is returned as the
 *             result of the call.
 * @param length
 * @param prot
 * @param flags
 * @param fd
 * @param offset
 * @return
 */
CRTDECL(void*,
mmap(
        _In_ void*  addr,
        _In_ size_t length,
        _In_ int    prot,
        _In_ int    flags,
        _In_ int    fd,
        _In_ off_t  offset));

/**
 * @brief Deletes the mappings for the specified
 * address range, and causes further references to addresses within
 * the range to generate invalid memory references.  The region is
 * also automatically unmapped when the process is terminated.  On
 * the other hand, closing the file descriptor does not unmap the
 * region.
 * @param addr The address addr must be a multiple of the page size
 * @param length
 * @return
 */
CRTDECL(int,
munmap(
        _In_ void*  addr,
        _In_ size_t length));

/**
 * @brief Flushes changes made to the in-core copy of a file that
 * was mapped into memory using mmap back to the filesystem.
 * Without use of this call, there is no guarantee that changes are
 * written back before munmap is called.  To be more precise, the
 * part of the file that corresponds to the memory area starting at
 * addr and having length length is updated.
 * @param addr
 * @param length
 * @param flags
 * @return
 */
CRTDECL(int,
msync(
        _In_ void*  addr,
        _In_ size_t length,
        _In_ int    flags));

#endif //!__SYS_MMAN_H__
