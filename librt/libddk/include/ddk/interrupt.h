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
#define INTERRUPT_ACPICONFORM_TRIGGERMODE     0x00000002U // If set, trigger mode means level-triggered
#define INTERRUPT_ACPICONFORM_POLARITY        0x00000004U // If set, polarity means active-low
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

// Only describes a line-based interrupt. 
// Message-based interrupts will use DeviceId instead.
typedef struct DeviceInterruptLine {
    int          Line;
    int          Pin;
    unsigned int AcpiConform;
} DeviceInterruptLine_t;

/**
 * @brief Architecture-composed message for one vector in an interrupt allocation.
 *
 * AddressLow and AddressHigh form the 64-bit message address. Data is the
 * message payload, not an allocation index or a PCI MSI-X table-entry index.
 * Fixed-width fields keep the syscall layout identical on all architectures.
 */
typedef struct DeviceInterruptMessage {
    uint32_t AddressLow;
    uint32_t AddressHigh;
    uint32_t Data;
} DeviceInterruptMessage_t;

// Each interrupt may carry its own resource table and an
// affinity mask.
typedef struct DeviceInterruptVector {
    InterruptResourceTable_t ResourceTable;
    uint32_t                 AffinityMask;
} DeviceInterruptVector_t;

// Describes all interrupts allocated for a single device.
// This structure includes the device identifier, line-based interrupt information,
// flags, the chosen strategy, the number of allocated vectors, the array of vectors,
// and the current state of the interrupt allocation.
typedef struct DeviceInterrupt {
    uuid_t                       DeviceId;
    DeviceInterruptLine_t        Line;
    unsigned int                 Flags;
    unsigned int                 IsPci;
    uint32_t                     Strategy;

    // These are maintained by libddk
    uint32_t                     Count;
    DeviceInterruptVector_t*     Vectors;
    struct DeviceInterruptState* State;
} DeviceInterrupt_t;

/**
 * @brief Initializes line and device information from the provided device base.
 * 
 * Pass NULL for device to set Line explicitly for a fixed platform interrupt.
 * IsPci is derived from the device bus type; only BusTypePci enables PCI MSI.
 * Must be called prior to Allocate. Flags defaults to shared delivery;
 * set INTERRUPT_EXCLUSIVE before allocation if this line must not be shared.
 */
DDKDECL(void,
DeviceInterruptInitialize(
    _Out_ DeviceInterrupt_t* interrupt,
    _In_  BusDevice_t*       device));

/**
 * @brief Reserves interrupts without installing a handler or enabling delivery.
 *
 * deviced prepares PCI MSI with MSI and INTx disabled. The driver must stop its
 * interrupt sources and set up bus mastering and MMIO.
 * 
 * Strategies are preferences in MSI-X, MSI, INTx order. This implementation
 * supports single-message PCI MSI and INTx. @p min must be one.
 * Multi-message MSI are unsupported. @p optimal may be larger so callers
 * can request higher counts when support is added.
 * Success returns the actual count. Failure leaves @p countOut zero and retains
 * any existing allocation. If allocation rollback fails, State may remain set,
 * including when allocating local vector storage fails after kernel reservation.
 * Retry Destroy until cleanup succeeds before discarding the object. An object
 * can own only one allocation at a time. Serialize calls on the object and keep
 * the device stopped during allocation and teardown.
 *
 * On x86, a released MSI vector that has been enabled is retired until reboot
 * because the platform cannot drain messages yet. Disable/Enable reuses the
 * reservation. Each Destroy/Allocate cycle uses another slot in the vector pool.
 */
DDKDECL(oserr_t,
DeviceInterruptAllocate(
    _InOut_ DeviceInterrupt_t* interrupt,
    _In_    uint32_t           min,
    _In_    uint32_t           optimal,
    _In_    uint32_t           strategies,
    _Out_   uint32_t*          countOut));

/**
 * @brief Reads the message for one vector without changing delivery or ownership.
 *
 * The index addresses the allocation's vector array; it does not identify a PCI
 * MSI-X table entry. The provider will define that mapping when MSI-X is enabled.
 * INTx has no message and returns OS_ENOTSUPPORTED. Failure leaves messageOut
 * unchanged. Serialize this query with other operations on the allocation.
 *
 * @param interrupt Successfully allocated interrupt object.
 * @param index Zero-based index less than Count.
 * @param messageOut Receives the architecture-composed address and data.
 * @return OS_EOK on success, or an error for an invalid or unavailable vector.
 */
DDKDECL(oserr_t,
DeviceInterruptGetMessage(
    _In_  DeviceInterrupt_t*        interrupt,
    _In_  uint32_t                  index,
    _Out_ DeviceInterruptMessage_t* messageOut));

/**
 * @brief Specifies acceptable CPUs before enabling the allocation.
 * INTx currently preserves platform routing: only UINT32_MAX (the default,
 * meaning no additional restriction) is supported. Other masks return
 * OS_ENOTSUPPORTED rather than claiming a CPU assignment was made.
 */
DDKDECL(oserr_t,
DeviceInterruptSetAffinity(
    _InOut_ DeviceInterrupt_t* interrupt,
    _In_    uint32_t           index,
    _In_    uint32_t           affinityMask));

/**
 * @brief Sets a vector's fast handler while the allocation is disabled.
 * The handler is mapped and checked by the kernel during Enable, after all
 * resources have been described. A missing handler prevents Enable succeeding.
 */
DDKDECL(oserr_t,
DeviceInterruptSetHandler(
    _InOut_ DeviceInterrupt_t* interrupt,
    _In_    uint32_t           index,
    _In_    InterruptHandler_t handler));

/**
 * @brief Gives one vector an event descriptor for deferred processing.
 * The descriptor and all registered resources must remain alive until Disable
 * or Destroy succeeds, because a running fast handler can still access them.
 */
DDKDECL(oserr_t,
DeviceInterruptSetDescriptor(
    _InOut_ DeviceInterrupt_t* interrupt,
    _In_    uint32_t           index,
    _In_    int                descriptor));

/**
 * @brief Adds an acquired I/O region that the kernel maps for this handler.
 * Returns OS_EOOM if the vector's resource table is full, so a driver never
 * enables a handler with a silently omitted register range.
 */
DDKDECL(oserr_t,
DeviceInterruptAddIoResource(
    _InOut_ DeviceInterrupt_t* interrupt,
    _In_    uint32_t           index,
    _In_    DeviceIo_t*        ioSpace));

/**
 * @brief Adds memory that must remain accessible while the fast handler runs.
 * Address and length must describe a nonempty range without wrapping. The
 * kernel creates its own mapping, which the handler reads through its table.
 */
DDKDECL(oserr_t,
DeviceInterruptAddMemoryResource(
    _InOut_ DeviceInterrupt_t* interrupt,
    _In_    uint32_t           index,
    _In_    uintptr_t          address,
    _In_    size_t             length,
    _In_    unsigned int       flags));

/**
 * @brief Maps all handlers and resources before allowing them to run.
 * The driver must keep its device's interrupt source stopped during setup and
 * start device work only after success. Repeated Enable calls are harmless.
 * Kernel dispatch opens before deviced enables PCI MSI. If enabling MSI fails
 * or the response is lost, cleanup stops PCI delivery before waiting for handlers
 * and unmapping them. If cleanup fails, Disable or Destroy must release the
 * remaining resources.
 */
DDKDECL(oserr_t,
DeviceInterruptEnable(
    _InOut_ DeviceInterrupt_t* interrupt));

/**
 * @brief Stops handler delivery and waits for running handlers to finish.
 * Stop interrupts at the device first: a shared INTx line may remain enabled
 * for another device. On success, handler mappings are released but the line
 * reservation remains, allowing configuration changes and a later Enable.
 * For PCI MSI, deviced disables the capability before the kernel waits for
 * handlers. The driver must still stop its queues, DMA and other interrupt sources.
 * Call from normal driver code, never from a fast interrupt handler.
 */
DDKDECL(oserr_t,
DeviceInterruptDisable(
    _InOut_ DeviceInterrupt_t* interrupt));

/**
 * @brief Disables delivery and releases the allocation and local storage.
 * Safe on an initialized or zeroed object and on repeated calls. If teardown
 * fails, ownership is retained so the caller can retry without losing resources.
 * Device identity and routing remain available for a later Allocate.
 * Retry a failed MSI Destroy until it succeeds. Enable rejects the object
 * because teardown may already have restored its PCI configuration.
 */
DDKDECL(oserr_t,
DeviceInterruptDestroy(
    _InOut_ DeviceInterrupt_t* interrupt));

#endif //!__INTERRUPT_INTERFACE_H__
