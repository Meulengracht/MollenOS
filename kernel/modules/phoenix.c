/* MollenOS
 *
 * Copyright 2018, Philip Meulengracht
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
 * Alias & Process Management
 * - The implementation of phoenix is responsible for managing alias's, handle
 *   file events and creating/destroying processes.
 */
#define __MODULE "PROC"
#define __TRACE

#include <garbagecollector.h>
#include <criticalsection.h>
#include <os/mollenos.h>
#include <os/file.h>
#include <threading.h>
#include <machine.h>
#include <handle.h>
#include <assert.h>
#include <debug.h>
#include <heap.h>

/* PhoenixFileHandler
 * Handles new file-mapping events that occur through unmapped page events. */
OsStatus_t
PhoenixFileHandler(
    _In_Opt_ void*  Context)
{
    MCoreAshFileMappingEvent_t *Event   = (MCoreAshFileMappingEvent_t*)Context;
    MCoreAshFileMapping_t *Mapping      = NULL;
    LargeInteger_t Value;

    Event->Result = OsError;
    foreach(Node, Event->Process->FileMappings) {
        Mapping = (MCoreAshFileMapping_t*)Node->Data;
        if (ISINRANGE(Event->Address, Mapping->BufferObject.Address, (Mapping->BufferObject.Address + Mapping->Length) - 1)) {
            Flags_t MappingFlags    = MAPPING_USERSPACE | MAPPING_FIXED | MAPPING_PROVIDED;
            size_t BytesIndex       = 0;
            size_t BytesRead        = 0;
            size_t Offset;
            if (!(Mapping->Flags & FILE_MAPPING_WRITE)) {
                MappingFlags |= MAPPING_READONLY;
            }
            if (Mapping->Flags & FILE_MAPPING_EXECUTE) {
                MappingFlags |= MAPPING_EXECUTABLE;
            }

            // Allocate a page for this transfer
            Mapping->BufferObject.Dma = AllocateSystemMemory(GetSystemMemoryPageSize(), __MASK, MEMORY_DOMAIN);
            if (Mapping->BufferObject.Dma == 0) {
                return OsSuccess;
            }

            // Calculate the file offset, but it has to be page-aligned
            Offset          = (Event->Address - Mapping->BufferObject.Address);
            Offset         -= Offset % GetSystemMemoryPageSize();

            // Create the mapping
            Value.QuadPart  = Mapping->FileBlock + Offset; // File offset in page-aligned blocks
            Event->Result = CreateSystemMemorySpaceMapping(Event->Process->MemorySpace, 
                &Mapping->BufferObject.Dma, &Event->Address, GetSystemMemoryPageSize(), MappingFlags, __MASK);

            // Seek to the file offset, then perform the read of one-page size
            if (SeekFile(Mapping->FileHandle, Value.u.LowPart, Value.u.HighPart) == FsOk && 
                ReadFile(Mapping->FileHandle, Mapping->BufferObject.Handle, GetSystemMemoryPageSize(), &BytesIndex, &BytesRead) == FsOk) {
                Event->Result = OsSuccess;
            }
        }
    }

    // Ignore invalid requests
    SchedulerHandleSignal((uintptr_t*)Event);
    return OsSuccess;
}

/* PhoenixFileMappingEvent
 * Signals a new file-mapping access event to the phoenix process system. */
void
PhoenixFileMappingEvent(
    _In_ MCoreAshFileMappingEvent_t* Event) {
    GcSignal(GcFileHandleId, Event);
}
