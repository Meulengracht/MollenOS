/* MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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
 * MollenOS MCore - System Calls
 */
#define __MODULE "SCIF"
//#define __TRACE

#include <process/phoenix.h>
#include <process/process.h>
#include <memorybuffer.h>
#include <ds/mstring.h>
#include <os/file.h>
#include <handle.h>
#include <debug.h>
#include <heap.h>

/* ScGetWorkingDirectory
 * Queries the current working directory path for the current process (See _MAXPATH) */
OsStatus_t
ScGetWorkingDirectory(
    _In_ UUId_t     ProcessId,
    _In_ char*      PathBuffer,
    _In_ size_t     MaxLength)
{
    SystemProcess_t*    Process;
    size_t              BytesToCopy = MaxLength;
    if (ProcessId == UUID_INVALID) {
        Process = GetCurrentProcess();
    }
    else {
        Process = GetProcess(ProcessId);
    }

    // Sanitize parameters
    if (Process == NULL || PathBuffer == NULL || MaxLength == 0) {
        return OsError;
    }
    
    BytesToCopy = MIN(strlen(MStringRaw(Process->WorkingDirectory)) + 1, MaxLength);
    memcpy(PathBuffer, MStringRaw(Process->WorkingDirectory), BytesToCopy);
    return OsSuccess;
}

/* ScSetWorkingDirectory
 * Performs changes to the current working directory by canonicalizing the given 
 * path modifier or absolute path */
OsStatus_t
ScSetWorkingDirectory(
    _In_ const char* Path)
{
    SystemProcess_t*    Process = GetCurrentProcess();
    MString_t*          Translated;

    if (Process == NULL || Path == NULL) {
        return OsError;
    }
    Translated = MStringCreate((void*)Path, StrUTF8);
    MStringDestroy(Process->WorkingDirectory);
    Process->WorkingDirectory = Translated;
    return OsSuccess;
}

/* ScGetAssemblyDirectory
 * Queries the application path for the current process (See _MAXPATH) */
OsStatus_t
ScGetAssemblyDirectory(
    _In_ char*      PathBuffer,
    _In_ size_t     MaxLength)
{
    SystemProcess_t*    Process     = GetCurrentProcess();
    size_t              BytesToCopy = MaxLength;

    if (Process == NULL || PathBuffer == NULL || MaxLength == 0) {
        return OsError;
    }
    if (strlen(MStringRaw(Process->BaseDirectory)) + 1 < MaxLength) {
        BytesToCopy = strlen(MStringRaw(Process->BaseDirectory)) + 1;
    }
    memcpy(PathBuffer, MStringRaw(Process->BaseDirectory), BytesToCopy);
    return OsSuccess;
}

/* Parameter structure for creating file-mappings. 
 * Private structure, only used for parameter passing. */
struct FileMappingParameters {
    UUId_t    FileHandle;
    int       Flags;
    uint64_t  Offset;
    size_t    Size;
};

/* ScCreateFileMapping
 * Creates a new file-mapping that are bound to a specific file-descriptor. 
 * Accessing this mapping will be proxied to the specific file-access */
OsStatus_t
ScCreateFileMapping(
    _In_  struct FileMappingParameters* Parameters,
    _Out_ void**                        MemoryPointer)
{
    MCoreAshFileMapping_t*  Mapping;
    SystemProcess_t*        Process         = GetCurrentProcess();
    size_t                  AdjustedSize    = Parameters->Size + (Parameters->Offset % GetSystemMemoryPageSize());

    if (Process == NULL || Parameters == NULL || Parameters->Size == 0) {
        return OsError;
    }
    
    // Create a dma object used for file interactions, but make sure the
    // mappings are not created (yet). We just want the physical and virtual
    // connections done
    Mapping = (MCoreAshFileMapping_t*)kmalloc(sizeof(MCoreAshFileMapping_t));
    memset((void*)Mapping, 0, sizeof(MCoreAshFileMapping_t));
    if (CreateMemoryBuffer(MEMORY_BUFFER_FILEMAPPING, AdjustedSize, &Mapping->BufferObject) != OsSuccess) {
        kfree(Mapping);
        return OsError;
    }

    // Create a new mapping
    // We only read in block-sizes of page-size, this means the
    Mapping->Flags          = (Flags_t)Parameters->Flags;
    Mapping->FileHandle     = Parameters->FileHandle;

    // Calculate offsets
    Mapping->FileBlock      = (Parameters->Offset / GetSystemMemoryPageSize()) * GetSystemMemoryPageSize();
    Mapping->BlockOffset    = (Parameters->Offset % GetSystemMemoryPageSize());
    Mapping->Length         = AdjustedSize;
    CollectionAppend(Process->FileMappings, &Mapping->Header);

    // Update out
    *MemoryPointer = (void*)(Mapping->BufferObject.Address + Mapping->BlockOffset);
    return OsSuccess;
}

/* ScDestroyFileMapping
 * Destroys a previously created file-mapping using it's counterpart. */
OsStatus_t
ScDestroyFileMapping(
    _In_ void *MemoryPointer)
{
    MCoreAshFileMapping_t*  Mapping;
    CollectionItem_t*       Node;
    LargeInteger_t          Value;
    SystemProcess_t*        Process = GetCurrentProcess();

    // Only processes are allowed to call this
    if (Process == NULL) {
        return OsError;
    }

    // Iterate and find the node first
    _foreach(Node, Process->FileMappings) {
        Mapping = (MCoreAshFileMapping_t*)Node;
        if (ISINRANGE((uintptr_t)MemoryPointer, Mapping->BufferObject.Address, (Mapping->BufferObject.Address + Mapping->Length) - 1)) {
            break; // Continue to unmap process
        }
    }

    // Proceed to cleanup if node was found
    if (Node != NULL) {
        CollectionRemoveByNode(Process->FileMappings, Node);
        
        // Unmap all mappings done
        for (uintptr_t ItrAddress = Mapping->BufferObject.Address; 
            ItrAddress < (Mapping->BufferObject.Address + Mapping->Length); 
            ItrAddress += GetSystemMemoryPageSize()) {
            
            // Get the physical mapping in question
            Mapping->BufferObject.Dma = GetSystemMemoryMapping(GetCurrentSystemMemorySpace(), ItrAddress);
            if (Mapping->BufferObject.Dma == 0) {
                continue;
            }
            
            // Check if page should be flushed to disk
            if (IsSystemMemoryPageDirty(GetCurrentSystemMemorySpace(), ItrAddress) == OsSuccess) {
                size_t BytesToFlush     = MIN((Mapping->BufferObject.Address + Mapping->Length) - ItrAddress, GetSystemMemoryPageSize());
                Value.QuadPart          = Mapping->FileBlock + (ItrAddress - Mapping->BufferObject.Address);
                
                if (SeekFile(Mapping->FileHandle, Value.u.LowPart, Value.u.HighPart) != FsOk ||
                    WriteFile(Mapping->FileHandle, Mapping->BufferObject.Handle, BytesToFlush, NULL) != FsOk) {
                    ERROR("Failed to flush file mapping, file most likely is closed or doesn't exist");
                }
            }
            RemoveSystemMemoryMapping(GetCurrentSystemMemorySpace(), ItrAddress, GetSystemMemoryPageSize());
        }

        // Free the mapping from the heap
        ReleaseBlockmapRegion(Process->Heap, Mapping->BufferObject.Address, Mapping->Length);
        DestroyHandle(Mapping->BufferObject.Handle);
        kfree(Mapping);
    }
    return Node != NULL ? OsSuccess : OsError;
}

/* ScDestroyHandle
 * Destroys a generic os handle. Error checks before calling the os version. */
OsStatus_t
ScDestroyHandle(
    _In_ UUId_t Handle)
{
    if (Handle == 0 || Handle == UUID_INVALID) {
        return OsError;
    }
    return DestroyHandle(Handle);
}
