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

#include <internal/_syscalls.h>
#include <os/memory.h>
#include <os/mollenos.h>

static size_t g_pageSize  = 0;

oserr_t
MemoryAllocate(
    _In_  void*        Hint,
    _In_  size_t       Length,
    _In_  unsigned int Flags,
    _Out_ void**       MemoryOut)
{
	if (!Length || !MemoryOut) {
		return OS_EINVALPARAMS;
	}
	return Syscall_MemoryAllocate(Hint, Length, Flags, MemoryOut);
}

oserr_t
MemoryFree(
	_In_ void*  Memory,
	_In_ size_t Length)
{
	if (!Length || !Memory) {
		return OS_EINVALPARAMS;
	}
	return Syscall_MemoryFree(Memory, Length);
}

oserr_t
MemoryProtect(
    _In_  void*         Memory,
	_In_  size_t        Length,
    _In_  unsigned int  Flags,
    _Out_ unsigned int* PreviousFlags)
{
	if (!Length || !Memory) {
		return OS_EINVALPARAMS;
	}
    return Syscall_MemoryProtect(Memory, Length, Flags, PreviousFlags);
}

oserr_t
MemoryQueryAllocation(
        _In_ void*                 Memory,
        _In_ OSMemoryDescriptor_t* DescriptorOut)
{
    if (!Memory || !DescriptorOut) {
        return OS_EINVALPARAMS;
    }
    return Syscall_MemoryQueryAllocation(Memory, DescriptorOut);
}

oserr_t
MemoryQueryAttributes(
        _In_ void*         Memory,
        _In_ size_t        Length,
        _In_ unsigned int* AttributeArray)
{
    if (!Memory || !Length || !AttributeArray) {
        return OS_EINVALPARAMS;
    }
    return Syscall_MemoryQueryAttributes(Memory, Length, AttributeArray);
}

size_t
MemoryPageSize(void)
{
    if (!g_pageSize) {
        OSSystemMemoryInfo_t memoryInfo;
        size_t               bytesQueried;
        oserr_t              oserr;

        oserr = OSSystemQuery(
                OSSYSTEMQUERY_MEMINFO,
                &memoryInfo,
                sizeof(OSSystemMemoryInfo_t),
                &bytesQueried
        );
        if (oserr != OS_EOK) {
            g_pageSize = 0x1000;
            return g_pageSize;
        }

        g_pageSize = memoryInfo.PageSizeBytes;
    }
    return g_pageSize;
}
