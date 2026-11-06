# Kernel memory

This directory contains the kernel memory allocators, address spaces and shared
buffers. Architecture-specific page tables and MMU operations are in
`kernel/arch/`.

| Location | Responsibility |
| --- | --- |
| `heap.c` | Kernel slab caches and heap allocation. |
| `memory.c` | Boot memory, physical allocator setup and initial kernel mappings. |
| `memory_region.c` | Select device memory regions from the loader memory map. |
| `space/` | Virtual address spaces, shared process contexts and tracked allocations. Mapping, unmapping, user copies, protection changes and cross-core synchronization. |
| `shm/` | Shared buffer storage, restricted views, access reservations and device mappings. |

`space/private.h` describes address-space contexts and tracked allocations.
`shm/private.h` describes shared buffer storage. Restricted address-space
allocation checks also use `shm/range.h`. See `shm/device_private.h` for
device address translation.
