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
 * MollenOS MCore - Interrupt Support Definitions & Structures
 * - This header describes the base interrupt-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __INTERRUPT_INTERFACE_H__
#define __INTERRUPT_INTERFACE_H__

#include <os/osdefs.h>
#include <os/io.h>

#define INTERRUPT_NONE                      (int)-1
#define INTERRUPT_MAXVECTORS                8
#define INTERRUPT_MAX_MEMORY_RESOURCES      4
#define INTERRUPT_MAX_IO_RESOURCES          4

#define INTERRUPT_RESOURCE_DISABLE_CACHE    0x00000001

// Fast-interrupts
// Fast-interrupt handlers are called from an interrupt context and therefore
// are very limited in terms of what they can do. They have full access to their memory space
// however they are not allowed to do any waiting, functions calls or any lengthy operations.
#ifndef __INTERRUPTHANDLER
#define __INTERRUPTHANDLER
typedef struct _FastInterruptResources FastInterruptResources_t;
typedef InterruptStatus_t(*InterruptHandler_t)(FastInterruptResources_t*, void*);
#endif

// Fast-Interrupt Memory Resource
typedef struct _FastInterruptMemoryResource {
    uintptr_t                       Address;
    size_t                          Length;
    Flags_t                         Flags;
} FastInterruptMemoryResource_t;

// Fast-Interrupt Resource Table
// Table that descripes the executable region of the fast interrupt handler, and the
// memory resources the fast interrupt handler needs access too. Validation and security
// measures will be taken on the passed regions, and interrupt-copies will be created for the handler.
typedef struct _FastInterruptResourceTable {
    InterruptHandler_t              Handler;
    DeviceIo_t*                     IoResources[INTERRUPT_MAX_IO_RESOURCES];
    FastInterruptMemoryResource_t   MemoryResources[INTERRUPT_MAX_MEMORY_RESOURCES];
} FastInterruptResourceTable_t;

// Fast-Interrupt
// Fast-interrupts are severely limited in what they can access, the interrupt table provides access
// to pre-mapped regions that was requested when the interrupt was registed. The table can provide access
// to some memory regions, io-regions and some system-functions (like the standard input pipe).
typedef struct _FastInterruptResources {
    // Resource Table
    FastInterruptResourceTable_t*   ResourceTable;

    // System Functions
    size_t                          (*ReadIoSpace)(DeviceIo_t*, size_t Offset, size_t Length);
    OsStatus_t                      (*WriteIoSpace)(DeviceIo_t*, size_t Offset, size_t Value, size_t Length);
} FastInterruptResources_t;

#define INTERRUPT_IOSPACE(Resources, Index)     Resources->ResourceTable->IoResources[Index]
#define INTERRUPT_RESOURCE(Resources, Index)    Resources->ResourceTable->MemoryResources[Index].Address

// Interrupt register options
#define INTERRUPT_SOFT                  0x00000001  // Interrupt is not triggered by a hardware line
#define INTERRUPT_VECTOR                0x00000002  // Interrupt can be either values set in the Vector
#define INTERRUPT_MSI                   0x00000004  // Interrupt uses MSI to deliver
#define INTERRUPT_NOTSHARABLE           0x00000008  // Interrupt line can not be shared
#define INTERRUPT_USERSPACE             0x00000010  // Send interrupt notification to process

/* The interrupt descriptor structure, this contains
 * information about the interrupt that needs to be registered
 * and special handling. */
typedef struct _DeviceInterrupt {
    // Interrupt-handler(s) and context
    // FastHandler is called to determine whether or not this source
    // has produced the interrupt.
    FastInterruptResourceTable_t    FastInterrupt;

    // General information, note that these can change
    // after the RegisterInterruptSource, always use the value
    // in <Line> to see your allocated interrupt-line
    Flags_t                         AcpiConform;
    int                             Line;
    int                             Pin;
    void*                           Context;

    // If the system should choose the best available
    // between all directs, fill all unused entries with 
    // INTERRUPT_NONE. Specify INTERRUPT_VECTOR to use this.
    int                             Vectors[INTERRUPT_MAXVECTORS];

    // Read-Only
    uintptr_t                       MsiAddress;     // INTERRUPT_MSI - The address of MSI
    uintptr_t                       MsiValue;       // INTERRUPT_MSI - The value of MSI
} DeviceInterrupt_t;

/* RegisterFastInterruptHandler
 * Registers a fast interrupt handler associated with the interrupt. */
CRTDECL(void,
RegisterFastInterruptHandler(
    _In_ DeviceInterrupt_t* Interrupt,
    _In_ InterruptHandler_t Handler));

/* RegisterFastInterruptIoResource
 * Registers the given device io resource with the fast-interrupt. */
CRTDECL(void,
RegisterFastInterruptIoResource(
    _In_ DeviceInterrupt_t* Interrupt,
    _In_ DeviceIo_t*        IoSpace));

/* RegisterFastInterruptMemoryResource
 * Registers the given memory resource with the fast-interrupt. */
CRTDECL(void,
RegisterFastInterruptMemoryResource(
    _In_ DeviceInterrupt_t* Interrupt,
    _In_ uintptr_t          Address,
    _In_ size_t             Length,
    _In_ Flags_t            Flags));

/* RegisterInterruptContext
 * Sets the context pointer that should be attached to any OnInterrupt events. */
CRTDECL(void,
RegisterInterruptContext(
    _In_ DeviceInterrupt_t* Interrupt,
    _In_ void*              Context));

/* RegisterInterruptSource 
 * Allocates the given interrupt source for use by the requesting driver, an id for the interrupt source
 * is returned. After a succesful register, OnInterrupt can be called by the event-system */
CRTDECL(UUId_t,
RegisterInterruptSource(
    _In_ DeviceInterrupt_t* Interrupt,
    _In_ Flags_t            Flags));

/* UnregisterInterruptSource 
 * Unallocates the given interrupt source and disables all events of OnInterrupt */
CRTDECL(OsStatus_t,
UnregisterInterruptSource(
    _In_ UUId_t             Source));

#endif //!_INTERRUPT_INTERFACE_H_
