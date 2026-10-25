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

#include <ddk/types/interrupt.h>

DECL_STRUCT(DeviceIo);
DECL_STRUCT(BusDevice);
DECL_STRUCT(Device);

/**
 * @brief One driver-owned interrupt allocation and its handler configuration.
 * Initialize before use. Flags and SourceIndex are pre-allocation inputs;
 * DeviceId, IsPci and legacy Line routing are initialized from the device.
 * Kind, Count, Vectors and State are maintained by libddk and must not be edited.
 * A platform source always occupies vector zero, even when SourceIndex is nonzero.
 * Use setters to configure Vectors rather than editing their resource tables.
 * Serialize all operations outside fast-interrupt context and retain the object
 * after failed cleanup. Initialization must not overwrite a live allocation.
 */
typedef struct DeviceInterrupt {
    // Published device identity, or UUID_INVALID for an explicitly supplied line.
    uuid_t                       DeviceId;
    // Legacy/manual routing; platform source routing comes from its provider.
    DeviceInterruptLine_t        Line;
    // Pre-allocation input: zero for shared lines or INTERRUPT_EXCLUSIVE.
    unsigned int                 Flags;
    // Derived from BusTypePci during initialization; do not override.
    unsigned int                 IsPci;
    // Platform firmware source index; use DeviceInterruptSelectSource.
    uint32_t                     SourceIndex;

    // Allocation results and private ownership state.
    InterruptSourceKind_t        Kind;
    uint32_t                     Count;
    DeviceInterruptVector_t*     Vectors;
    struct DeviceInterruptState* State;
} DeviceInterrupt_t;

/**
 * @brief Initializes line and device information from the provided device base.
 * 
 * Pass NULL for device to set Line explicitly for a manual/legacy line interrupt.
 * Passing a platform Device_t supplies its published identity directly.
 * Allocate queries the bus for the selected firmware source. Handler/resource
 * setters use allocation vector indices; a platform source uses vector zero.
 * The handler must clear or mask its peripheral before returning; any upstream
 * acknowledgement is supplied by the bus provider without driver involvement.
 * IsPci is derived from the device bus type; only BusTypePci enables PCI MSI.
 * Must be called prior to Allocate. Flags defaults to shared delivery;
 * set INTERRUPT_EXCLUSIVE before allocation if this line must not be shared.
 * This clears the object and allocates nothing. Do not call it to reset an
 * object that still owns State; complete Destroy first.
 *
 * @param interrupt Storage for a new interrupt object; must be non-NULL.
 * @param device Published Device_t/BusDevice_t, or NULL for manual line routing.
 * @return No result; allocation and validation occur in Allocate.
 */
DDKDECL(void,
DeviceInterruptInitialize(
    _Out_ DeviceInterrupt_t* interrupt,
    _In_  Device_t*          device));

/**
 * @brief Select one platform interrupt from the provider's firmware order.
 * Each source uses a separate DeviceInterrupt object and one kernel group.
 * This applies equally to direct lines and sources translated into messages.
 * Selection allocates nothing and is unavailable while this object owns a group.
 * Allocation validates the index against the provider. PCI devices and manual
 * lines do not have a selectable firmware source list.
 * @param interrupt Initialized platform device interrupt descriptor.
 * @param index Firmware interrupt-list index.
 * @return OS_EOK, OS_EBUSY for an existing allocation, or invalid arguments.
 */
DDKDECL(oserr_t,
DeviceInterruptSelectSource(
    _InOut_ DeviceInterrupt_t* interrupt,
    _In_ uint32_t index));

/**
 * @brief Reserve interrupts without installing a handler or enabling delivery.
 *
 * Platform devices reserve the selected firmware source as one vector and
 * require min == 1. Call DevicePrepare before querying an activated platform
 * provider. The provider validates direct lines or attaches/programs translated
 * messages with delivery blocked. SourceIndex is never a handler vector index.
 * Manual line objects also reserve one vector without a provider lease.
 *
 * PCI devices default to MSI-X, single-message MSI, then INTx. Drivers that only
 * support particular PCI modes must use DeviceInterruptAllocatePci from
 * pci-interrupt.h. Only unsupported capabilities and kernel vector exhaustion
 * permit fallback; transport, configuration and provider conflicts are errors.
 * Stop device interrupt sources during setup. Configure queues/causes before
 * Enable; for PCI MSI-X vector i corresponds to table entry i.
 *
 * Failure sets countOut to zero and preserves any existing allocation. A failed
 * rollback can leave State set, even if local vector storage allocation fails.
 * Retry Destroy until successful before discarding the object. One object owns
 * one allocation; serialize lifecycle calls and keep the device stopped during
 * teardown. Disable synchronizes/unbinds handlers before Destroy releases the
 * provider lease, which may retain platform clocks, reset and power dependencies.
 *
 * Enabled user message vectors can be retired until reboot when the platform
 * cannot drain delayed writes. Disable/Enable reuses the existing reservation;
 * repeated Destroy/Allocate cycles can consume the available message pool.
 *
 * @param interrupt Initialized object with no existing allocation.
 * @param min Minimum acceptable count; one for platform or manual sources.
 * @param optimal Preferred count, at least min; platform sources still use one.
 * @param countOut Actual count on success, zero on failure.
 * @return OS_EOK or an error; a retained State requires Destroy cleanup.
 */
DDKDECL(oserr_t,
DeviceInterruptAllocate(
    _InOut_ DeviceInterrupt_t* interrupt,
    _In_    uint32_t           min,
    _In_    uint32_t           optimal,
    _Out_   uint32_t*          countOut));

/**
 * @brief Reads the message for one vector without changing delivery or ownership.
 *
 * The index addresses the allocation's vector array. The initial PCI MSI-X
 * provider assigns this vector to the table entry with the same index.
 * Line sources have no message and return OS_ENOTSUPPORTED. Failure leaves messageOut
 * unchanged. Serialize this query with other operations on the allocation.
 * A translated platform source also has a message, but its provider owns the
 * upstream programming. Reading it does not transfer that responsibility.
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
 * Current backends preserve platform routing: only UINT32_MAX (the default,
 * meaning no additional restriction) is supported. Other masks return
 * OS_ENOTSUPPORTED rather than claiming a CPU assignment was made.
 *
 * @param interrupt Successfully allocated, editable interrupt object.
 * @param index Allocation vector index, less than Count.
 * @param affinityMask UINT32_MAX for existing routing; zero is invalid.
 * @return OS_EOK, OS_ENOTSUPPORTED for other nonzero masks, OS_EBUSY while
 *         bindings or cleanup prevent edits, or OS_EINVALPARAMS.
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
 * Follow InterruptHandler_t's code/resource restrictions; ordinary callbacks
 * that access driver globals or call library functions are not suitable.
 *
 * @param interrupt Successfully allocated, editable interrupt object.
 * @param index Allocation vector index, less than Count.
 * @param handler Non-NULL fast-handler entry point, retained until cleanup succeeds.
 * @return OS_EOK, OS_EBUSY when not editable, or OS_EINVALPARAMS.
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
 * libddk resolves the file descriptor to a native event handle stored in the
 * resource table; a fast handler signals it through EventSignal.
 *
 * @param interrupt Successfully allocated, editable interrupt object.
 * @param index Allocation vector index, less than Count.
 * @param descriptor Nonnegative event descriptor with a valid native handle.
 * @return OS_EOK, OS_EBUSY when not editable, or OS_EINVALPARAMS.
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
 * Entries occupy the first unused slot and retain that index during binding.
 * Keep the descriptor and acquired mapping alive until cleanup succeeds.
 *
 * @param interrupt Successfully allocated, editable interrupt object.
 * @param index Allocation vector index, less than Count.
 * @param ioSpace Non-NULL acquired I/O region used by the fast handler.
 * @return OS_EOK, OS_EOOM when no slots remain, OS_EBUSY when not editable,
 *         or OS_EINVALPARAMS.
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
 * Binding preserves array indices and shares the original backing memory.
 * Keep that backing alive until Disable or Destroy succeeds.
 *
 * @param interrupt Successfully allocated, editable interrupt object.
 * @param index Allocation vector index, less than Count.
 * @param address Nonzero start address in the driver's address space.
 * @param length Nonzero accessible byte count without address wraparound.
 * @param flags Zero or INTERRUPT_RESOURCE_DISABLE_CACHE.
 * @return OS_EOK, OS_EOOM when no slots remain, OS_EBUSY when not editable,
 *         or OS_EINVALPARAMS for an invalid range or flags.
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
 * Kernel dispatch opens before the provider enables upstream delivery. If that fails
 * or the response is lost, cleanup stops provider delivery before waiting for handlers
 * and unmapping them. If cleanup fails, Disable or Destroy must release the
 * remaining resources.
 * An ordinary failed Disable is completed before a subsequent Enable rebinds
 * handlers. An object whose Destroy has started cannot be enabled again.
 *
 * @param interrupt Successfully allocated object with every handler configured.
 * @return OS_EOK when delivery is enabled, OS_EBUSY when setup/teardown prevents
 *         reuse, or a handler, mapping, controller, or provider error.
 */
DDKDECL(oserr_t,
DeviceInterruptEnable(
    _InOut_ DeviceInterrupt_t* interrupt));

/**
 * @brief Stops handler delivery and waits for running handlers to finish.
 * Stop interrupts at the device first: a shared INTx line may remain enabled
 * for another device. On success, handler mappings are released but the group
 * reservation remains, allowing configuration changes and a later Enable.
 * When the provider controls upstream hardware, deviced masks it before kernel
 * cleanup. Direct native lines are stopped by the kernel instead. The driver
 * must still stop its queues, DMA and other interrupt sources.
 * Call from normal driver code, never from a fast interrupt handler.
 * A failure retains cleanup state even for manual or PCI INTx lines. A later
 * Enable finishes the incomplete stop before rebinding and reopening dispatch.
 * Keep handler resources alive until Disable or Destroy succeeds.
 *
 * @param interrupt Initialized object; no allocation is a successful no-op.
 * @return OS_EOK after provider stop and kernel handler cleanup, OS_EINVALPARAMS
 *         for NULL, or a retryable provider/controller/unmapping error.
 */
DDKDECL(oserr_t,
DeviceInterruptDisable(
    _InOut_ DeviceInterrupt_t* interrupt));

/**
 * @brief Disables delivery and releases the allocation and local storage.
 * Safe on an initialized or zeroed object and on repeated calls. If teardown
 * fails, ownership is retained so the caller can retry without losing resources.
 * Device identity and routing remain available for a later Allocate.
 * Once Destroy starts, setters and Enable reject the object for every source
 * kind, even if a later Disable succeeds. Finish Destroy, then Allocate again;
 * teardown may already have released part of the kernel or provider state.
 * Successful Destroy clears allocation results, vector storage and State;
 * it preserves the device identity, input flags and selected source for reuse.
 *
 * @param interrupt Initialized or zeroed object; must be non-NULL.
 * @return OS_EOK after cleanup or when no allocation exists, OS_EINVALPARAMS
 *         for NULL, or a cleanup error requiring another Destroy attempt.
 */
DDKDECL(oserr_t,
DeviceInterruptDestroy(
    _InOut_ DeviceInterrupt_t* interrupt));

#endif //!__INTERRUPT_INTERFACE_H__
