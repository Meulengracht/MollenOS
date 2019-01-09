
#if 0
/* PhoenixFileHandler
 * Handles new file-mapping events that occur through unmapped page events. */
OsStatus_t
PhoenixFileHandler(
    _In_Opt_ void*  Context)
{
    SystemFileMappingEvent_t* Event   = (SystemFileMappingEvent_t*)Context;
    SystemFileMapping_t*      Mapping = NULL;
    LargeInteger_t            Value;

    Event->Result = OsError;
    foreach(Node, Event->MemorySpace->FileMappings) {
        Mapping = (SystemFileMapping_t*)Node->Data;
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
            Event->Result = CreateSystemMemorySpaceMapping(Event->MemorySpace, 
                &Mapping->BufferObject.Dma, &Event->Address, GetSystemMemoryPageSize(), MappingFlags, __MASK);

            // Seek to the file offset, then perform the read of one-page size
            if (SeekFile(Mapping->FileHandle, Value.u.LowPart, Value.u.HighPart) == FsOk && 
                ReadFile(Mapping->FileHandle, Mapping->BufferObject.Handle, GetSystemMemoryPageSize(), &BytesIndex, &BytesRead) == FsOk) {
                Event->Result = OsSuccess;
            }
        }
    }
    SchedulerHandleSignal((uintptr_t*)Event);
    return OsSuccess;
}
#endif

#if 0
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
#endif
