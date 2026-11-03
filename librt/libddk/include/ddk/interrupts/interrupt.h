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
 * @brief Interrupt allocation and handler configuration for a driver.
 * Initialize before use. Set Flags and SourceIndex before allocation.
 * DeviceId, IsPci and legacy Line routing are initialized from the device.
 * libddk sets Kind, Count, Vectors and State. Leave these fields unchanged.
 * Use the setters to configure Vectors without editing their resource tables.
 * A platform source always uses vector zero, even when SourceIndex is nonzero.
 * Calls must be serialized and made outside a fast interrupt handler. If cleanup
 * fails, keep the object so cleanup can be retried. Complete Destroy before
 * initializing an object that still has an allocation.
 */
typedef struct DeviceInterrupt {
    // Published device identity, or UUID_INVALID for an explicitly supplied line.
    uuid_t                       DeviceId;
    // Legacy/manual routing. The bus supplies platform source routing.
    DeviceInterruptLine_t        Line;
    // Pre-allocation input: zero for shared lines or INTERRUPT_EXCLUSIVE.
    unsigned int                 Flags;
    // Set from BusTypePci during initialization. Leave unchanged.
    unsigned int                 IsPci;
    // Platform firmware source index. Use DeviceInterruptSelectSource.
    uint32_t                     SourceIndex;

    // Allocation results and internal cleanup state.
    InterruptSourceKind_t        Kind;
    uint32_t                     Count;
    DeviceInterruptVector_t*     Vectors;
    struct DeviceInterruptState* State;
} DeviceInterrupt_t;

/**
 * @brief Initializes line and device information from the device base.
 *
 * Call before Allocate. Pass NULL for device to configure Line manually for a
 * legacy interrupt. A platform Device_t supplies its published identity.
 * Allocate queries the bus for the selected firmware source. Handler and
 * resource setters take vector indices. A platform source uses vector zero.
 *
 * The handler must clear or mask its peripheral before returning. The bus
 * handles any acknowledgement needed upstream. IsPci is set from the device
 * bus type, and only BusTypePci enables PCI MSI.
 *
 * Initialization clears the object without allocating resources. Flags defaults
 * to shared delivery. Set INTERRUPT_EXCLUSIVE before allocation if the line
 * must not be shared. If State is still set, complete Destroy first.
 *
 * @param interrupt Non-NULL storage for a new interrupt object.
 * @param device Published Device_t/BusDevice_t, or NULL for manual line routing.
 * @return No result. Allocate performs allocation and validation.
 */
DDKDECL(void,
DeviceInterruptInitialize(
    _Out_ DeviceInterrupt_t* interrupt,
    _In_  Device_t*          device));

/**
 * @brief Selects a platform interrupt by its position in the firmware list.
 *
 * Each source needs a separate DeviceInterrupt object and one kernel group,
 * including direct lines and sources translated into messages. Selection does
 * not allocate resources. The object must have no allocated group.
 * Allocate checks the index with the bus. PCI devices and manual lines have
 * no selectable firmware source list.
 *
 * @param interrupt Initialized platform device interrupt descriptor.
 * @param index Firmware interrupt-list index.
 * @return OS_EOK, OS_EBUSY for an existing allocation, or invalid arguments.
 */
DDKDECL(oserr_t,
DeviceInterruptSelectSource(
    _InOut_ DeviceInterrupt_t* interrupt,
    _In_ uint32_t index));

/**
 * @brief Reserves interrupts without installing a handler or enabling delivery.
 *
 * Platform devices reserve the selected firmware source as one vector and
 * require min == 1. Call DevicePrepare before querying a platform bus that
 * requires preparation. The bus validates direct lines or attaches and programs
 * translated messages with delivery blocked. SourceIndex selects the firmware
 * source, while handler setters use vector zero.
 * Manual line objects also reserve one vector, without a reservation from the bus.
 *
 * PCI devices try MSI-X, single-message MSI, then INTx. If the driver supports
 * only certain PCI modes, use DeviceInterruptAllocatePci from interrupt-pci.h.
 * Fallback is allowed for unsupported capabilities or exhausted kernel vectors.
 * Transport, configuration and bus conflicts return errors.
 * Stop device interrupt sources during setup. Configure queues and interrupt
 * causes before Enable. For PCI MSI-X, vector i maps to table entry i.
 *
 * On failure, countOut is zero and any existing allocation stays unchanged.
 * If rollback fails, State can remain set even when vector storage could not be
 * allocated. Keep the object and retry Destroy until it succeeds.
 * Each object holds one allocation. Serialize lifecycle calls and keep the
 * device stopped during teardown. Disable waits for handlers and unbinds them
 * before Destroy releases the bus reservation. That reservation may keep
 * platform clocks, reset and power resources in use.
 *
 * If the platform cannot drain delayed writes, enabled user message vectors
 * may be unavailable for reuse until reboot. Disable/Enable uses the existing
 * reservation. Repeated Destroy/Allocate cycles can exhaust the message pool.
 *
 * @param interrupt Initialized object with no existing allocation.
 * @param min Minimum acceptable count. Must be one for platform or manual sources.
 * @param optimal Preferred count, at least min. Platform sources still use one.
 * @param countOut Actual count on success, zero on failure.
 * @return OS_EOK or an error. If State remains set, call Destroy to clean up.
 */
DDKDECL(oserr_t,
DeviceInterruptAllocate(
    _InOut_ DeviceInterrupt_t* interrupt,
    _In_    uint32_t           min,
    _In_    uint32_t           optimal,
    _Out_   uint32_t*          countOut));

/**
 * @brief Reads the message for one vector without changing delivery or allocation.
 *
 * The index selects an entry in the allocation's vector array. The initial
 * PCI MSI-X implementation uses the same index for its table entry.
 * Line sources return OS_ENOTSUPPORTED because they have no message. Failure
 * leaves messageOut unchanged. Serialize this query with other allocation calls.
 * A translated platform source also has a message. The bus remains responsible
 * for programming the upstream hardware after the message is read.
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
 * Current implementations keep the platform routing. Only UINT32_MAX is
 * supported, which is the default and adds no restriction. Other nonzero
 * masks return OS_ENOTSUPPORTED.
 *
 * @param interrupt Successfully allocated, editable interrupt object.
 * @param index Allocation vector index, less than Count.
 * @param affinityMask UINT32_MAX for existing routing. Zero is invalid.
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
 * resources have been described. Enable fails if a handler is missing.
 * Follow InterruptHandler_t's code and resource restrictions. The handler must
 * not access driver globals or call library functions.
 *
 * @param interrupt Successfully allocated, editable interrupt object.
 * @param index Allocation vector index, less than Count.
 * @param handler Non-NULL fast-handler entry point, valid until cleanup succeeds.
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
 * resource table. A fast handler signals it through EventSignal.
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
 * The vector must have a free resource slot. Returns OS_EOOM if the table is full.
 * Each entry uses the first free slot and keeps that index during binding.
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
 * Kernel dispatch starts before the bus enables upstream delivery. If enabling
 * delivery fails or the response is lost, cleanup stops delivery at the bus
 * before waiting for handlers and unmapping them. If cleanup fails, call Disable
 * or Destroy to release the remaining resources.
 * An ordinary failed Disable is completed before a subsequent Enable rebinds
 * handlers. An object whose Destroy has started cannot be enabled again.
 *
 * @param interrupt Successfully allocated object with every handler configured.
 * @return OS_EOK when delivery is enabled, OS_EBUSY when setup/teardown prevents
 *         reuse, or a handler, mapping, controller, or bus error.
 */
DDKDECL(oserr_t,
DeviceInterruptEnable(
    _InOut_ DeviceInterrupt_t* interrupt));

/**
 * @brief Stops handler delivery and waits for running handlers to finish.
 * Stop interrupts at the device first: a shared INTx line may remain enabled
 * for another device. On success, handler mappings are released but the group
 * reservation remains, allowing configuration changes and a later Enable.
 * When the bus controls upstream hardware, deviced masks it before kernel
 * cleanup. Direct native lines are stopped by the kernel instead. The driver
 * must still stop its queues, DMA and other interrupt sources.
 * Call from normal driver code, never from a fast interrupt handler.
 * On failure, cleanup state stays available even for manual or PCI INTx lines.
 * A later Enable finishes the incomplete stop before rebinding and reopening
 * dispatch.
 * Keep handler resources alive until Disable or Destroy succeeds.
 *
 * @param interrupt Initialized object. No allocation is a successful no-op.
 * @return OS_EOK after bus delivery stops and kernel handler cleanup finishes,
 *         OS_EINVALPARAMS for NULL, or a retryable bus, controller or unmapping error.
 */
DDKDECL(oserr_t,
DeviceInterruptDisable(
    _InOut_ DeviceInterrupt_t* interrupt));

/**
 * @brief Disables delivery and releases the allocation and local storage.
 *
 * The object must be initialized or zeroed. Repeated calls are safe. If cleanup
 * fails, the object keeps the state needed to retry.
 * Once Destroy starts, setters and Enable reject the object for every source
 * kind, even if a later Disable succeeds. Complete Destroy before calling
 * Allocate again because part of the kernel or bus state may already be gone.
 * On success, allocation results, vector storage and State are cleared.
 * Device identity, routing, input flags and the selected source stay available
 * for another Allocate call.
 *
 * @param interrupt Non-NULL initialized or zeroed object.
 * @return OS_EOK after cleanup or when no allocation exists, OS_EINVALPARAMS
 *         for NULL, or a cleanup error requiring another Destroy attempt.
 */
DDKDECL(oserr_t,
DeviceInterruptDestroy(
    _InOut_ DeviceInterrupt_t* interrupt));

#endif //!__INTERRUPT_INTERFACE_H__
