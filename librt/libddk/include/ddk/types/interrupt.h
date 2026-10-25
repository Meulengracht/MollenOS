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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Interrupt Support Definitions & Structures
 * - This header describes the base interrupt-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __DDK_TYPES_INTERRUPT_H__
#define __DDK_TYPES_INTERRUPT_H__

#include <ddk/ddkdefs.h>

DECL_STRUCT(DeviceIo);

#define INTERRUPT_NONE                      (int)-1
// Legacy candidate-vector list size, not the negotiated message-group limit.
#define INTERRUPT_MAXVECTORS                8
#define INTERRUPT_MAX_MEMORY_RESOURCES      4
#define INTERRUPT_MAX_IO_RESOURCES          4

#define INTERRUPT_RESOURCE_DISABLE_CACHE    0x00000001U

/**
 * Fast handlers run in kernel interrupt context using explicitly mapped code
 * and resources, not the driver's ordinary address space. For user handlers,
 * context points to the kernel's InterruptResourceTable_t; use its translated
 * resource addresses instead of captured pointers or driver globals.
 *
 * Keep the handler self-contained within its mapped code window. Use the
 * supplied function table for external services; do not call ordinary library
 * functions, block, allocate, or issue IPC. Defer longer work by signaling the
 * registered event. The driver must clear or mask its peripheral's cause before
 * returning. Provider completion callbacks follow the same restrictions.
 */
#ifndef __INTERRUPTHANDLER
#define __INTERRUPTHANDLER
typedef struct InterruptFunctionTable InterruptFunctionTable_t;
typedef irqstatus_t(*InterruptHandler_t)(InterruptFunctionTable_t*, void*);
#endif

/**
 * A shared memory range to map for a handler. Address and Length describe the
 * driver's existing backing memory, which must remain alive until successful
 * disable/cleanup. Binding creates another mapping, not a copy of its contents.
 * An unused entry has every field zero; a used entry requires a nonzero address
 * and length without wraparound. Flags accepts INTERRUPT_RESOURCE_DISABLE_CACHE.
 */
typedef struct FastInterruptMemoryResource {
    uintptr_t    Address;
    size_t       Length;
    unsigned int Flags;
} FastInterruptMemoryResource_t;

/**
 * Handler description copied at binding. The kernel maps Handler code and each
 * resource into addresses usable by the fast handler, preserving array indices.
 * The caller retains ownership of backing code, memory, I/O and event resources.
 * Failed cleanup can retain mappings, so keep that backing alive until cleanup
 * succeeds. The handler receives the translated table as its context.
 */
typedef struct InterruptResourceTable {
    // Required non-NULL entry point; ordinary process callbacks are unsuitable.
    InterruptHandler_t            Handler;
    // Native event handle, not a file descriptor; UUID_INVALID if unused.
    uuid_t                        HandleResource;
    // Acquired I/O descriptors; NULL entries are unused.
    DeviceIo_t*                   IoResources[INTERRUPT_MAX_IO_RESOURCES];
    FastInterruptMemoryResource_t MemoryResources[INTERRUPT_MAX_MEMORY_RESOURCES];
} InterruptResourceTable_t;

/**
 * Kernel services callable from a fast handler. ReadIoSpace and WriteIoSpace
 * use the translated I/O descriptors in its resource table. EventSignal wakes
 * deferred processing through HandleResource. Trace is for bounded diagnostics;
 * it does not make ordinary library calls or blocking operations safe.
 */
typedef struct InterruptFunctionTable {
    size_t  (*ReadIoSpace)(DeviceIo_t*, size_t offset, size_t length);
    oserr_t (*WriteIoSpace)(DeviceIo_t*, size_t offset, size_t value, size_t length);
    oserr_t (*EventSignal)(uuid_t handle);
    void    (*Trace)(const char* format, ...);
} InterruptFunctionTable_t;

#define INTERRUPT_IOSPACE(Resources, Index)     Resources->IoResources[Index]
#define INTERRUPT_RESOURCE(Resources, Index)    Resources->MemoryResources[Index].Address

/**
 * Line-routing attributes used by both ACPI and native firmware providers.
 * PRESENT marks supplied attributes; TRIGGERMODE and POLARITY select level and
 * active-low behavior. FIXED marks an exact line; it does not by itself supply
 * or validate the owning controller required by InterruptSourceFixedLine.
 */
#define INTERRUPT_ACPICONFORM_PRESENT         0x00000001U
#define INTERRUPT_ACPICONFORM_TRIGGERMODE     0x00000002U // If set, trigger mode means level-triggered
#define INTERRUPT_ACPICONFORM_POLARITY        0x00000004U // If set, polarity means active-low
#define INTERRUPT_ACPICONFORM_SHAREABLE       0x00000008U
#define INTERRUPT_ACPICONFORM_FIXED           0x00000010U

// Low-level reservation options. Driver/group requests accept only EXCLUSIVE;
// the allocator derives message and kernel routing flags from the resource kind.
#define INTERRUPT_SOFT      0x00000001U  // Interrupt is not triggered by a hardware line
#define INTERRUPT_VECTOR    0x00000002U  // Interrupt can be either values set in the Vector
#define INTERRUPT_MSI       0x00000004U  // Interrupt uses MSI to deliver
#define INTERRUPT_EXCLUSIVE 0x00000008U  // Interrupt line can not be shared

/**
 * Hardware line and optional legacy PCI pin routing. INTERRUPT_NONE denotes an
 * unspecified line or unused pin; fixed sources use Pin == INTERRUPT_NONE.
 * AcpiConform carries INTERRUPT_ACPICONFORM_* attributes. Message allocation
 * requests require the entire structure to be zero instead of INTERRUPT_NONE.
 */
typedef struct DeviceInterruptLine {
    int          Line;
    int          Pin;
    unsigned int AcpiConform;
} DeviceInterruptLine_t;

/**
 * Kernel-composed destination address (high/low halves) and payload written by
 * the device to signal one message interrupt. These are allocation results,
 * not caller-selected routing or proof of ownership. A line has no message.
 */
typedef struct DeviceInterruptMessage {
    uint32_t AddressLow;
    uint32_t AddressHigh;
    uint32_t Data;
} DeviceInterruptMessage_t;

/**
 * One allocation vector's deferred handler configuration. ResourceTable is
 * bound during Enable. AffinityMask currently accepts only UINT32_MAX, meaning
 * the existing platform routing; it does not change hardware CPU affinity.
 */
typedef struct DeviceInterruptVector {
    InterruptResourceTable_t ResourceTable;
    uint32_t                 AffinityMask;
} DeviceInterruptVector_t;

typedef enum InterruptSourceKind {
    // No successful allocation or source-query result.
    InterruptSourceInvalid,
    // Legacy/manual line routing, including PCI INTx pin routing.
    InterruptSourceLine,
    // A fixed line whose owning controller must match explicitly.
    InterruptSourceFixedLine,
    // Independently allocated messages from a kernel-owned controller.
    InterruptSourceMessage
} InterruptSourceKind_t;

#endif //!__DDK_TYPES_INTERRUPT_H__
