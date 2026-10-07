/**
 * Copyright, Philip Meulengracht
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

#ifndef __INTERRUPT_INTERFACE_H__
#define __INTERRUPT_INTERFACE_H__

#include <ddk/ddkdefs.h>

DECL_STRUCT(DeviceIo);
DECL_STRUCT(BusDevice);

#define INTERRUPT_NONE                      (int)-1
#define INTERRUPT_MAXVECTORS                8
#define INTERRUPT_MAX_MEMORY_RESOURCES      4
#define INTERRUPT_MAX_IO_RESOURCES          4

#define INTERRUPT_RESOURCE_DISABLE_CACHE    0x00000001U

// Fast-interrupts
// Fast-interrupt handlers are called from an interrupt context and therefore
// are very limited in terms of what they can do. They have full access to their memory space
// however they are not allowed to do any waiting, functions calls or any lengthy operations.
#ifndef __INTERRUPTHANDLER
#define __INTERRUPTHANDLER
typedef struct InterruptFunctionTable InterruptFunctionTable_t;
typedef irqstatus_t(*InterruptHandler_t)(InterruptFunctionTable_t*, void*);
#endif

// Fast-Interrupt Memory Resource
typedef struct FastInterruptMemoryResource {
    uintptr_t    Address;
    size_t       Length;
    unsigned int Flags;
} FastInterruptMemoryResource_t;

// Fast-Interrupt Resource Table
// Table that descripes the executable region of the fast interrupt handler, and the
// memory resources the fast interrupt handler needs access too. Validation and security
// measures will be taken on the passed regions, and interrupt-copies will be created for the handler.
typedef struct InterruptResourceTable {
    InterruptHandler_t            Handler;
    uuid_t                        HandleResource;
    DeviceIo_t*                   IoResources[INTERRUPT_MAX_IO_RESOURCES];
    FastInterruptMemoryResource_t MemoryResources[INTERRUPT_MAX_MEMORY_RESOURCES];
} InterruptResourceTable_t;

// Fast-Interrupt
// Fast-interrupts are severely limited in what they can access, the interrupt table provides access
// to pre-mapped regions that was requested when the interrupt was registed. The table can provide access
// to some memory regions, io-regions and some system-functions (like the standard input pipe).
typedef struct InterruptFunctionTable {
    size_t  (*ReadIoSpace)(DeviceIo_t*, size_t offset, size_t length);
    oserr_t (*WriteIoSpace)(DeviceIo_t*, size_t offset, size_t value, size_t length);
    oserr_t (*EventSignal)(uuid_t handle);
    void    (*Trace)(const char* format, ...);
} InterruptFunctionTable_t;

#define INTERRUPT_IOSPACE(Resources, Index)     Resources->IoResources[Index]
#define INTERRUPT_RESOURCE(Resources, Index)    Resources->MemoryResources[Index].Address

/*
 * ACPI Conform flags
 * This is essentially some bonus information that is
 * needed when registering interrupts
 */
#define INTERRUPT_ACPICONFORM_PRESENT         0x00000001U
#define INTERRUPT_ACPICONFORM_TRIGGERMODE     0x00000002U
#define INTERRUPT_ACPICONFORM_POLARITY        0x00000004U
#define INTERRUPT_ACPICONFORM_SHAREABLE       0x00000008U
#define INTERRUPT_ACPICONFORM_FIXED           0x00000010U

// Interrupt register options
#define INTERRUPT_SOFT      0x00000001U  // Interrupt is not triggered by a hardware line
#define INTERRUPT_VECTOR    0x00000002U  // Interrupt can be either values set in the Vector
#define INTERRUPT_MSI       0x00000004U  // Interrupt uses MSI to deliver
#define INTERRUPT_EXCLUSIVE 0x00000008U  // Interrupt line can not be shared

#define INTERRUPT_STRATEGY_INTx 0x1
#define INTERRUPT_STRATEGY_MSI  0x2
#define INTERRUPT_STRATEGY_MSIX 0x4
#define INTERRUPT_STRATEGY_ANY  0x7

#define DEVICE_MSI_CONTROLLER_MIP 0x1U

typedef struct DeviceMsiControllerDescription {
    unsigned int Type;
    uint32_t     ProviderId;
    uint32_t     Segment;
    uint8_t      BusStart;
    uint8_t      BusEnd;
    int          ParentLine;
    uint32_t     MessageOffset;
    uint32_t     MessageCount;
    uint64_t     DoorbellAddress;
    uint64_t     DoorbellLength;
} DeviceMsiControllerDescription_t;

typedef struct DeviceInterrupt {
    // Interrupt-handler(s) and context
    // FastHandler is called to determine whether or not this source
    // has produced the interrupt.
    InterruptResourceTable_t ResourceTable;

    // General information, note that these can change
    // after the RegisterInterruptSource, always use the value
    // in <Line> to see your allocated interrupt-line
    unsigned int AcpiConform;
    int          Line;
    int          Pin;
    void*        Context;

    // If the system should choose the best available
    // between all directs, fill all unused entries with 
    // INTERRUPT_NONE. Specify INTERRUPT_VECTOR to use this.
    int Vectors[INTERRUPT_MAXVECTORS];

    // Pci Identification
    unsigned int IsPci;
    unsigned int Segment;
    unsigned int Bus;
    unsigned int Slot;
    unsigned int Function;
    uuid_t      DeviceId;

    // Kernel output metadata for controller-owned MSI routes.
    uint32_t MsiControllerId;
    uint32_t MsiHwIrq;
    uint16_t MsiIndex;
    int      MsiParentLine;
    uint32_t MsiRouteFlags;

    // Msi Identification
    uint64_t  MsiAddress;     // INTERRUPT_MSI - The address of MSI
    uintptr_t MsiValue;       // INTERRUPT_MSI - The value of MSI
} DeviceInterrupt_t;

typedef struct DeviceInterruptQuiesceRequest {
    uuid_t      Token;
    uuid_t      DeviceId;
    uint32_t    Segment;
    uint8_t     Bus;
    uint8_t     Slot;
    uint8_t     Function;
    uint8_t     Reserved;
} DeviceInterruptQuiesceRequest_t;

typedef uuid_t DeviceInterruptSet_t;

/**
 * @brief Registers a group of interrupt sources and returns a handle that owns them.
 * Registration is all-or-nothing. The resolved line and MSI message fields are
 * copied back into each descriptor on success.
 *
 * @param interrupts The interrupt descriptors to register.
 * @param count The number of descriptors to register, up to INTERRUPT_MAXVECTORS.
 * @param flags The interrupt registration flags applied to every descriptor.
 * @param setOut Receives the handle used to unregister the complete set.
 * @return OS_EOK if every source was registered, or an error code if registration failed.
 */
DDKDECL(oserr_t,
DeviceInterruptSetRegister(
    _In_   DeviceInterrupt_t*     interrupts,
    _In_   uint32_t                count,
    _In_   unsigned int            flags,
    _Out_  DeviceInterruptSet_t*   setOut));

/**
 * @brief Unregisters every interrupt source owned by the set and waits for
 * in-flight handlers to complete.
 *
 * @param interruptSet The set handle returned by DeviceInterruptSetRegister.
 * @return OS_EOK if the set was destroyed, or an error code if the handle was invalid.
 */
DDKDECL(oserr_t,
DeviceInterruptSetDestroy(
    _In_ DeviceInterruptSet_t interruptSet));

/**
 * @brief Registers deviced's event descriptor for kernel MSI-quiesce requests.
 * This is a system-service interface; the first registering process owns the queue.
 */
DDKDECL(oserr_t,
DeviceInterruptQuiesceRegister(
    _In_ int eventDescriptor));

/**
 * @brief Retrieves the next pending PCI device that must be quiesced before its
 * quarantined MSI vectors can be reused.
 */
DDKDECL(oserr_t,
DeviceInterruptQuiesceNext(
    _Out_ DeviceInterruptQuiesceRequest_t* requestOut));

/**
 * @brief Acknowledges a device after deviced has disabled its MSI state and bus mastering.
 */
DDKDECL(oserr_t,
DeviceInterruptQuiesceComplete(
    _In_ uuid_t token));

/**
 * @brief Registers a firmware-described MSI controller for its PCI host range.
 * This is intended for deviced during host registration.
 * @return A kernel controller ID, or UUID_INVALID on failure.
 */
DDKDECL(uuid_t,
DeviceInterruptMsiControllerRegister(
    _In_ const DeviceMsiControllerDescription_t* description));

/**
 * @brief Initializes a new device interrupt instance based on the bus device descriptor.
 * The updated flow for v2 should be to allocate the needed vectors after this call.
 * 
 * @param interrupt The interrupt descriptor to initialize.
 * @param device The bus device descriptor associated with the interrupt.
 */
DDKDECL(void,
DeviceInterruptInitialize(
    _In_ DeviceInterrupt_t* interrupt,
    _In_ BusDevice_t*       device));

/**
 * @brief Allocates an interrupt set using the requested strategies.
 * The vectors array must contain optimal initialized descriptors. MSI-X may
 * allocate between min and optimal vectors. MSI and INTx currently allocate
 * one vector. The driver remains responsible for programming the device.
 *
 * @param vectors Per-vector descriptors containing handlers and fast resources.
 * @param min The minimum number of vectors required.
 * @param optimal The optimal number of vectors desired.
 * @param strategy The accepted INTERRUPT_STRATEGY_* values.
 * @param setOut Receives the kernel-owned set handle.
 * @param countOut Receives the number of vectors actually allocated.
 * @param strategyOut Receives the selected INTERRUPT_STRATEGY_* value.
 * @return OS_EOK if the allocation succeeded, or an error code if it failed.
 */
DDKDECL(oserr_t,
DeviceInterruptAllocate(
    _In_  DeviceInterrupt_t*    vectors,
    _In_  uint32_t              min,
    _In_  uint32_t              optimal,
    _In_  uint32_t              strategy,
    _Out_ DeviceInterruptSet_t* setOut,
    _Out_ uint32_t*             countOut,
    _Out_ uint32_t*             strategyOut));

/**
 * @brief Programs PCI MSI/MSI-X state from the messages returned by allocation.
 * Call after allocation has installed handlers and before allowing device work.
 * MSI currently supports one vector; MSI-X supports independent table entries.
 * Keep the device quiesced between allocation and this call; unprogram before
 * reconfiguration or destroying the interrupt set.
 *
 * @param device The PCI bus device whose interrupt state is programmed.
 * @param vectors The allocated vector descriptors containing MSI messages.
 * @param count The number of allocated vectors.
 * @param strategy The strategy returned by DeviceInterruptAllocate.
 * @return OS_EOK if the device was programmed, or an error code on failure.
 */
DDKDECL(oserr_t,
DeviceInterruptProgram(
    _In_ BusDevice_t*       device,
    _In_ DeviceInterrupt_t* vectors,
    _In_ uint32_t           count,
    _In_ uint32_t           strategy));

/**
 * @brief Masks device interrupt generation before destroying its interrupt set.
 * This does not prove that the device is quiescent or make immediate vector
 * reuse safe; the platform must provide that guarantee separately. Quiesce the
 * device's own interrupt sources before calling this function.
 *
 * @param device The PCI bus device to quiesce at the interrupt source.
 * @return OS_EOK if interrupt generation was disabled, or an error code.
 */
DDKDECL(oserr_t,
DeviceInterruptUnprogram(
    _In_ BusDevice_t* device));

/**
 * @brief Sets the CPU affinity for a specific interrupt vector. The index must be lower
 * than the value returned in DeviceInterruptAllocate's <countOut> parameter.
 * 
 * @param interrupt The interrupt descriptor to set the affinity for.
 * @param index The index of the interrupt vector to set the affinity for.
 * @param affinityMask The CPU affinity mask indicating which CPUs can handle the interrupt.
 */
DDKDECL(void,
DeviceInterruptSetAffinity(
    _In_ DeviceInterrupt_t* interrupt,
    _In_ uint32_t           index,
    _In_ BitSet_t*          affinityMask));

/**
 * @brief Registers a handler for the specific index. The index must be lower
 * than the value returned in DeviceInterruptAllocate's <countOut> parameter.
 * 
 * @param interrupt The interrupt descriptor to set the handler for.
 * @param index The index of the interrupt vector to associate with the handler.
 * @param handler The interrupt handler function to register.
 */
DDKDECL(void,
DeviceInterruptSetHandler(
    _In_ DeviceInterrupt_t* interrupt,
    _In_ uint32_t           index,
    _In_ InterruptHandler_t handler));

/**
 * @brief Enables the specified interrupt vector for the device. The index must be lower
 * than the value returned in DeviceInterruptAllocate's <countOut> parameter.
 * 
 * @param interrupt The interrupt descriptor to enable the vector for.
 * @param index The index of the interrupt vector to enable.
 */
DDKDECL(void,
DeviceInterruptEnable(
    _In_ DeviceInterrupt_t* interrupt,
    _In_ uint32_t           index));

/**
 * @brief Tears down any interrupt vector associated with this device's
 * interrupt vectors. It additionally frees kernel resources associated
 * with this instance.
 * 
 * @param interrupt The interrupt descriptor to destroy.
 */
DDKDECL(void,
DeviceInterruptDestroy(
    _In_ DeviceInterrupt_t* interrupt));

/* RegisterFastInterruptHandler
 * Registers a fast interrupt handler associated with the interrupt. */
DDKDECL(void,
RegisterFastInterruptHandler(
    _In_ DeviceInterrupt_t* interrupt,
    _In_ InterruptHandler_t handler));

/* RegisterFastInterruptIoResource
 * Registers the given device io resource with the fast-interrupt. */
DDKDECL(void,
RegisterFastInterruptIoResource(
    _In_ DeviceInterrupt_t* interrupt,
    _In_ DeviceIo_t*        ioSpace));

/* RegisterFastInterruptMemoryResource
 * Registers the given memory resource with the fast-interrupt. */
DDKDECL(void,
RegisterFastInterruptMemoryResource(
    _In_ DeviceInterrupt_t* interrupt,
    _In_ uintptr_t          address,
    _In_ size_t             length,
    _In_ unsigned int       flags));

/**
 * Register event descriptor that will be signalled when a fast interrupt needs additional processing
 * @param interrupt  The interrupt descriptor that it should be bound to
 * @param descriptor The descriptor that should be available for the interrupt
 */
DDKDECL(void,
RegisterInterruptDescriptor(
    _In_ DeviceInterrupt_t* interrupt,
    _In_ int                descriptor));

/* RegisterInterruptSource 
 * Allocates the given interrupt source for use by the requesting driver, an id for the interrupt source
 * is returned. After a succesful register, SIGINT can be invoked by the event-system */
DDKDECL(uuid_t,
RegisterInterruptSource(
    _In_ DeviceInterrupt_t* interrupt,
    _In_ unsigned int       flags));

/* UnregisterInterruptSource 
 * Unallocates the given interrupt source and disables all events of SIGINT */
DDKDECL(oserr_t,
UnregisterInterruptSource(
    _In_ uuid_t interruptHandle));

#endif //!_INTERRUPT_INTERFACE_H_
