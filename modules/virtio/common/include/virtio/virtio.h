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
 * MollenOS MCore - Virtio Driver
 * - Contains the implementation of a shared virtio driver
 *   for all the virtio devices
 * Follows the specification here:
 *   https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html
 */

#ifndef __VIRTIO_VIRTIO_H__
#define __VIRTIO_VIRTIO_H__

#include <ddk/dma.h>
#include <ddk/devices/bus.h>
#include <os/spinlock.h>
#include <stdint.h>

/**
 * @brief Virtio PCI vendor ID.
 */
#define VIRTIO_PCI_VENDOR_ID 0x1AF4

/**
 * @brief Vendor-specific PCI capability type.
 */
#define VIRTIO_PCI_CAP_VENDOR_SPECIFIC 0x09

/**
 * @brief Virtio PCI capability types.
 * 
 * Capability regions:
 *  - Common configuration
 *  - Notifications
 *  - ISR Status
 *  - Device-specific configuration (optional)
 *  - PCI configuration access
 */
#define VIRTIO_PCI_CAP_COMMON_CFG        1
#define VIRTIO_PCI_CAP_NOTIFY_CFG        2
#define VIRTIO_PCI_CAP_ISR_CFG           3
#define VIRTIO_PCI_CAP_DEVICE_CFG        4
#define VIRTIO_PCI_CAP_PCI_CFG           5
#define VIRTIO_PCI_CAP_SHARED_MEMORY_CFG 8 
#define VIRTIO_PCI_CAP_VENDOR_CFG        9
// A device rarely supports more than this many capabilities
#define VIRTIO_PCI_CAP_COUNT             5

/**
 * @brief Virtio device status flags.
 *
 * Status starts at zero and returns to zero after reset. The driver must set
 * bits as it completes the initialization steps in section 3.1 of the spec.
 * Status bits must not be cleared individually. If FAILED is set, reset the
 * device before trying to initialize it again.
 *
 * If DEVICE_NEEDS_RESET is set, pending requests may or may not have completed.
 * The driver should reset the device to recover. Before DRIVER_OK is set, the
 * device must not consume buffers or send used-buffer notifications.
 *
 * The device should set DEVICE_NEEDS_RESET when an error requires a reset.
 * If DRIVER_OK is already set, it must then send a configuration change
 * notification to the driver.
 */
// Indicates that the guest OS has found the device and recognized it as a valid virtio device.
#define VIRTIO_STATUS_ACKNOWLEDGE        0x01
// Indicates that the guest OS knows how to drive the device.
#define VIRTIO_STATUS_DRIVER             0x02
// Indicates that the driver has successfully initialized the device.
#define VIRTIO_STATUS_DRIVER_OK          0x04
// Indicates that the driver has successfully negotiated features with the device.
#define VIRTIO_STATUS_FEATURES_OK        0x08
// Indicates that the device requires a reset.
#define VIRTIO_STATUS_DEVICE_NEEDS_RESET 0x40
// Indicates that the driver has encountered an error.
#define VIRTIO_STATUS_FAILED             0x80

/**
 * @brief Virtio feature flags.
 * During initialization, the driver reads the features offered by the device
 * and writes back the subset it supports. The device must be reset before
 * features can be negotiated again.
 *
 * Features are enabled only when both the device and driver support them, so
 * a new feature can be added without requiring both sides to be updated.
 *
 * Feature bits are allocated as follows:
 *  - 0 to 23, and 50 to 127
 *    Feature bits for the specific device type
 *  - 24 to 40
 *    Feature bits reserved for extensions to the queue and feature 
 *    negotiation mechanisms
 *  - 41 to 49, and 128 and above
 *    Feature bits reserved for future extensions.
 */

/**
 * @brief Indicates that the device complies with Virtio version 1.
 */
#define VIRTIO_F_VERSION_1 (1ULL << 32)

#define VIRTIO_F_RING_INDIRECT_DESC  (1ULL << 28)
#define VIRTIO_F_RING_EVENT_IDX      (1ULL << 29)
#define VIRTIO_F_RING_PACKED         (1ULL << 34)
#define VIRTIO_F_NOTIFICATION_DATA   (1ULL << 38)
#define VIRTIO_F_RING_RESET          (1ULL << 40)

#define VIRTIO_ISR_QUEUE_INTERRUPT  0x01
#define VIRTIO_ISR_CONFIG_INTERRUPT 0x02

/**
 * @brief Location and size of a Virtio PCI capability region.
 *
 * The device's PCI capability list identifies each region. Access it through
 * the function's BAR or through VIRTIO_PCI_CAP_PCI_CFG in PCI configuration
 * space. Fields are little-endian and read-only unless stated otherwise.
 */
PACKED_TYPESTRUCT(VirtioPciCapability32, {
    uint8_t  Vendor;            /* Generic PCI field: PCI_CAP_ID_VNDR */ 
    uint8_t  CapabilityNext;    /* Generic PCI field: next ptr. */ 
    uint8_t  CapabilityLength;  /* Generic PCI field: capability length */ 
    uint8_t  ConfigurationType; /* Identifies the structure. */ 
    uint8_t  Bar;               /* Where to find it. */ 
    uint8_t  Id;                /* Multiple capabilities of the same type */ 
    uint8_t  Padding[2];        /* Pad to full dword. */ 
    uint32_t Offset;            /* Offset within bar. */ 
    uint32_t Length;            /* Length of the structure, in bytes. */ 
});

PACKED_TYPESTRUCT(VirtioPciCapability64, { 
    VirtioPciCapability32_t Base;
    uint32_t                Offset; /* Bits 32-63 of the offset within bar. */ 
    uint32_t                Length; /* Bits 32-63 of the length of the structure, in bytes. */ 
});

/**
 * @brief Device and queue registers in the common PCI configuration region.
 *
 * DeviceFeature, NumQueues, ConfigGeneration, QueueNotifyOff and QueueNotifyData
 * are read-only. QueueSize must be nonzero for packed queues and a power of two
 * for split queues.
 *
 * Configure the queue fields before setting QueueEnable. After writing zero to
 * DeviceStatus, wait until it reads back as zero before initializing the device.
 *
 * The driver must not write zero to QueueEnable.
 *
 * If VIRTIO_F_RING_RESET was negotiated, write one to QueueReset and wait until
 * it reads back as zero. The writable queue fields can then be changed before
 * enabling the queue again by writing one to QueueEnable (see section 2.6.1).
 */
PACKED_TYPESTRUCT(VirtioPciCommonConfiguration, {
    /**
     * Device registers.
     */
    uint32_t DeviceFeatureSelect;   // RW, The driver uses this to select which feature bits device_feature shows. 
                                    // Value 0x0 selects Feature Bits 0 to 31, 0x1 selects Feature Bits 32 to 63, etc.
    uint32_t DeviceFeature;         // RO, The feature bits supported by the device for the selected feature word.
    uint32_t DriverFeatureSelect;   // RW, The driver uses this to select which feature bits driver_feature shows. 
                                    // Value 0x0 selects Feature Bits 0 to 31, 0x1 selects Feature Bits 32 to 63, etc.
    uint32_t DriverFeature;         // RW, The feature bits negotiated by the driver for the selected feature word.
    uint16_t ConfigMsiXVector;      // RW, The MSI-X vector for the device configuration.
    uint16_t NumQueues;             // RO, The number of virtqueues supported by the device.
    uint8_t  DeviceStatus;          // RW, The current status of the device.
    uint8_t  ConfigGeneration;      // RO, The generation number of the device configuration.

    /**
     * Selected queue registers.
     */
    uint16_t QueueSelect;           // RW, The index of the currently selected virtqueue.
    uint16_t QueueSize;             // RW, The size of the currently selected virtqueue.
    uint16_t QueueMsiXVector;       // RW, The MSI-X vector for the currently selected virtqueue.
    uint16_t QueueEnable;           // RW, Whether the currently selected virtqueue is enabled.
    uint16_t QueueNotifyOff;        // RO, The notify offset for the currently selected virtqueue.
    uint64_t QueueDesc;             // RW, The descriptor table address for the currently selected virtqueue.
    uint64_t QueueDriver;           // RW, The driver area address for the currently selected virtqueue.
    uint64_t QueueDevice;           // RW, The device area address for the currently selected virtqueue.
    uint16_t QueueNotifyData;       // RO, The notify data for the currently selected virtqueue.
    uint16_t QueueReset;            // RW, The reset status for the currently selected virtqueue.
});

/* This marks a buffer as continuing via the next field. */ 
#define VIRTIO_SPLIT_DESC_F_NEXT     0x0001
/* This marks a buffer as device write-only (otherwise device read-only). */ 
#define VIRTIO_SPLIT_DESC_F_WRITE    0x0002
/* This means the buffer contains a list of buffer descriptors. */ 
#define VIRTIO_SPLIT_DESC_F_INDIRECT 0x0004

/**
 * @brief Descriptor table entry used by a split virtqueue.
 */
PACKED_TYPESTRUCT(VirtioSplitDescriptor, {
    uint64_t Address; // Device address of the buffer.
    uint32_t Length;
    uint16_t Flags;   // Descriptor flags (VIRTIO_SPLIT_DESC_F_*)
    uint16_t Next;    // Next descriptor if Flags & VIRTIO_SPLIT_DESC_F_NEXT
});

/* Descriptor indicating that the driver should not be interrupted when a buffer is available. */
#define VIRTIO_SPLIT_AVAIL_F_NO_INTERRUPT 0x0001

/**
 * @brief Available-ring header written by the driver.
 *
 * QueueSize descriptor indices follow RingIndex. If VIRTIO_F_RING_EVENT_IDX
 * is negotiated, a uint16_t UsedEvent field follows those indices.
 */
PACKED_TYPESTRUCT(VirtioSplitAvailableRing, {
    uint16_t Flags;     // Available ring flags (VIRTIO_SPLIT_AVAIL_F_*)
    uint16_t RingIndex;
    uint16_t Ring[];
});

/**
 * @brief Element returned by the device in the used ring.
 */
PACKED_TYPESTRUCT(VirtioSplitUsedElement, {
    uint32_t DescriptorId;
    uint32_t Length;
});

/* Descriptor indicating that the device should not notify the driver. */
#define VIRTIO_SPLIT_USED_F_NO_NOTIFY     0x0001

/**
 * @brief Used-ring header written by the device.
 *
 * QueueSize VirtioSplitUsedElement_t values follow RingIndex. If
 * VIRTIO_F_RING_EVENT_IDX is negotiated, a uint16_t AvailableEvent field
 * follows those elements.
 */
PACKED_TYPESTRUCT(VirtioSplitUsedRing, {
    uint16_t                 Flags;     // Used ring flags (VIRTIO_SPLIT_USED_F_*)
    uint16_t                 RingIndex;
    VirtioSplitUsedElement_t Ring[];
});

/**
 * @brief Multiplier following the VIRTIO_PCI_CAP_NOTIFY_CFG capability.
 */
PACKED_TYPESTRUCT(VirtioPciNotifyCapability, {
    // notify_off_multiplier is combined with the queue_notify_off to 
    // derive the Queue Notify address within a BAR for a virtqueue:
    // cap.offset + queue_notify_off * notify_off_multiplier
    uint32_t NotifyOffMultiplier; 
});

/**
 * @brief ISR status capability for a Virtio PCI device.
 *
 * VIRTIO_PCI_CAP_ISR_CFG contains at least one byte for the INTx interrupt
 * status. Its offset has no alignment requirement. The status bits distinguish
 * queue interrupts from device configuration changes:
 *
 * Bits     0               1                               2 to 31
 * Purpose	Queue Interrupt	Device Configuration Interrupt	Reserved
 * 
 * Reading the register clears it and de-asserts the interrupt. When MSI-X is
 * enabled, the driver should not read ISR status to handle a queue interrupt.
 */

/**
 * @brief Vendor data for debugging and reporting.
 *
 * Check VendorId before interpreting or writing this data. The driver should
 * use this capability only for debugging and reporting.
 */
PACKED_TYPESTRUCT(VirtioPciVendorData, { 
    uint8_t  CapabilityVendor;   // Generic PCI field: PCI_CAP_ID_VNDR
    uint8_t  CapabilityNext;     // Generic PCI field: next ptr.
    uint8_t  CapabilityLength;   // Generic PCI field: capability length
    uint8_t  ConfigurationType;  // Identifies the structure.
    uint16_t VendorId;           // Identifies the PCI-SIG assigned Vendor ID
  /* For Vendor Definition */ 
  /* Pads structure to a multiple of 4 bytes */ 
  /* Reads must not have side effects */ 
});

/**
 * @brief BAR access through PCI configuration space.
 *
 * PciConfiguration follows the capability header. The driver can read and write
 * cap.Bar, cap.Length, cap.Offset and PciConfiguration. Set cap.Bar to the BAR,
 * cap.Length to an access size of 1, 2 or 4 bytes, and cap.Offset to the offset
 * within that BAR. PciConfiguration then accesses those bytes.
 *
 * cap.Offset must be a multiple of cap.Length. Before accessing PciConfiguration,
 * the entire range must fall within a BAR region described by another Virtio
 * capability whose type is not VIRTIO_PCI_CAP_PCI_CFG.
 */
PACKED_TYPESTRUCT(VirtioPciConfigurationCapability, {
    uint8_t PciConfiguration[4]; // Data for BAR access
});

/**
 * @brief I/O mapping, offset and length of a Virtio region within a PCI BAR.
 */
typedef struct VirtioPciRegion {
	DeviceIo_t* IoSpace;
	uint32_t    Offset;
	uint32_t    Length;
} VirtioPciRegion_t;

/**
 * @brief PCI mappings, DMA context and configuration state for a Virtio device.
 */
typedef struct VirtioPciTransport {
    // DMA context shared by queues, metadata and client buffer mappings.
    struct DMAContext* DmaContext;
    // Register access is no longer allowed once cleanup starts.
    bool Closing;
	BusDevice_t*      Device;
	VirtioPciRegion_t Regions[VIRTIO_PCI_CAP_COUNT];
	uint64_t          DeviceFeatures;
	uint64_t          DriverFeatures;
	uint32_t          NotifyOffsetMultiplier;
	uint32_t          ResetGeneration;
	uint16_t          ActiveQueues;
	uint8_t           AcquiredBars;
	// Set before creating queues. Remains set across Virtio reset while PCI
	// interrupts are enabled. All routes use entry 0.
	int               MsixEnabled;
	spinlock_t        ConfigurationLock;
} VirtioPciTransport_t;

typedef struct VirtioSplitQueue VirtioSplitQueue_t;

/**
 * @brief One contiguous device-address range referenced by a descriptor.
 */
typedef struct VirtioQueueBuffer {
    uint64_t Address;
    uint32_t Length;
    uint16_t Flags;
} VirtioQueueBuffer_t;

/**
 * @brief Completion information returned after consuming a used-ring entry.
 */
typedef struct VirtioQueueCompletion {
    void*    Context;
    uint32_t Length;
    uint16_t HeadDescriptor;
} VirtioQueueCompletion_t;

/**
 * @brief Split queue counters and ring indices.
 */
typedef struct VirtioQueueStats {
    uint64_t Submitted;
    uint64_t Completed;
    uint16_t QueueSize;
    uint16_t FreeDescriptors;
    uint16_t InFlight;
    uint16_t AvailableIndex;
    uint16_t UsedIndex;
} VirtioQueueStats_t;

_CODE_BEGIN

/**
 * @brief Initializes a modern Virtio PCI transport.
 *
 * Finds the Virtio vendor capabilities and checks their BAR ranges. Maps each
 * referenced BAR, then enables PCI memory access and bus mastering. Device
 * reset and feature negotiation must be done separately.
 *
 * The caller must keep @p device allocated until VirtioPciTransportDestroy()
 * succeeds. If initialization fails, cleanup is attempted. If cleanup also
 * fails, @p transport keeps the remaining resources so destroy can be retried.
 * Once transport cleanup starts, register access is no longer allowed.
 *
 * @param device The PCI bus device to initialize. Its vendor must be
 *               VIRTIO_PCI_VENDOR_ID.
 * @param transport Receives the initialized transport state.
 * @return OS_EOK on success, OS_EINVALPARAMS for invalid arguments or a
 *         non-Virtio vendor, OS_ENOTSUPPORTED if the required modern PCI
 *         capabilities are absent, or another error from PCI configuration,
 *         BAR mapping, or bus control.
 */
oserr_t
VirtioPciTransportInitialize(
	_In_  BusDevice_t*          device,
	_Out_ VirtioPciTransport_t* transport);

/**
 * @brief Releases resources held by a Virtio PCI transport.
 *
 * The hardware must be stopped and all queues destroyed first. Closes the DMA
 * context and releases the mapped BARs, then clears the transport. The caller
 * must free the referenced BusDevice_t separately. NULL and a cleared transport
 * are accepted.
 *
 * If DMA or BAR cleanup fails, the remaining resources are kept for retry.
 *
 * @param transport The transport to destroy.
 * @return OS_EOK or the first cleanup error. Active queues return OS_EBUSY.
 */
oserr_t
VirtioPciTransportDestroy(
	_In_ VirtioPciTransport_t* transport);

/**
 * @brief Disables bus mastering, MMIO/IO decoding and interrupts at the PCI level.
 *
 * Use this if VirtioPciReset() fails. It stops DMA through PCI bus control,
 * without relying on the Virtio status register. The transport cannot be used
 * afterwards.
 *
 * @param transport The initialized transport controlling the device.
 * @return OS_EOK on success, or the bus control error.
 */
oserr_t
VirtioPciTransportDisable(
	_In_ VirtioPciTransport_t* transport);

/**
 * @brief Reads a value from a mapped Virtio PCI capability region.
 *
 * @param region The capability region to access.
 * @param offset Byte offset relative to the beginning of the region.
 * @param width Access width in bytes. Must be 1, 2, 4, or 8.
 * @param valueOut Receives the value, zero-extended to 64 bits.
 * @return OS_EOK on success, OS_EINVALPARAMS if an argument, width, or range
 *         is invalid, or an error returned by the device I/O layer.
 */
oserr_t
VirtioPciRegionRead(
	_In_  const VirtioPciRegion_t* region,
	_In_  uint32_t                 offset,
	_In_  size_t                   width,
	_Out_ uint64_t*                valueOut);

/**
 * @brief Writes a value to a mapped Virtio PCI capability region.
 *
 * Only the least-significant @p width bytes of @p value are written.
 *
 * @param region The capability region to access.
 * @param offset Byte offset relative to the beginning of the region.
 * @param value The value to write.
 * @param width Access width in bytes. Must be 1, 2, 4, or 8.
 * @return OS_EOK on success, OS_EINVALPARAMS if an argument, width, or range
 *         is invalid, or an error returned by the device I/O layer.
 */
oserr_t
VirtioPciRegionWrite(
	_In_ const VirtioPciRegion_t* region,
	_In_ uint32_t                 offset,
	_In_ uint64_t                 value,
	_In_ size_t                   width);

/**
 * @brief Resets a Virtio device and waits for reset completion.
 *
 * The transport must be initialized first. Writes zero to device status, then
 * yields until status reads back as zero. Features must be negotiated again
 * and device configuration repeated after reset.
 *
 * @param transport The initialized transport controlling the device.
 * @return OS_EOK when reset completes, OS_EINVALPARAMS if @p transport is
 *         NULL, OS_EDEVFAULT if the device does not complete reset, or an I/O
 *         error from the common configuration region.
 */
oserr_t
VirtioPciReset(
	_In_ VirtioPciTransport_t* transport);

/**
 * @brief Resets the device and negotiates a set of Virtio features.
 *
 * Sets ACKNOWLEDGE, DRIVER and FEATURES_OK during initialization. Adds
 * VIRTIO_F_VERSION_1 to both the supported and required features, then accepts
 * only features supported by both the device and driver. The device must
 * support every required feature.
 *
 * On negotiation failure after reset, the device is marked FAILED. Queue and
 * device-specific setup must be completed before calling
 * VirtioPciFinishInitialization().
 *
 * @param transport The initialized transport controlling the device.
 * @param supportedFeatures Features understood by the driver.
 * @param requiredFeatures Supported features without which the driver cannot
 *                         operate. This must be a subset of
 *                         @p supportedFeatures.
 * @param negotiatedFeaturesOut Receives the accepted feature set.
 * @return OS_EOK on success, OS_EINVALPARAMS for invalid arguments or feature
 *         sets, OS_ENOTSUPPORTED if a required feature is unavailable or the
 *         device rejects FEATURES_OK, or another reset or device I/O error.
 */
oserr_t
VirtioPciNegotiateFeatures(
	_In_  VirtioPciTransport_t* transport,
	_In_  uint64_t              supportedFeatures,
	_In_  uint64_t              requiredFeatures,
	_Out_ uint64_t*             negotiatedFeaturesOut);

/**
 * @brief Completes device initialization after queue and device setup.
 *
 * Feature negotiation and queue and device setup must be complete first.
 * Sets DRIVER_OK, then checks that it was accepted and that FAILED and
 * DEVICE_NEEDS_RESET are clear. If MsixEnabled is set, configuration changes
 * are assigned to table entry zero before setting DRIVER_OK.
 *
 * @param transport The initialized and configured transport.
 * @return OS_EOK when DRIVER_OK is accepted, OS_EINVALPARAMS if @p transport
 *         is NULL, OS_EDEVFAULT if the vector is rejected or device status is invalid, or
 *         an I/O error from the common configuration region.
 */
oserr_t
VirtioPciFinishInitialization(
	_In_ VirtioPciTransport_t* transport);

/**
 * @brief Marks Virtio device initialization or operation as failed.
 *
 * Tries to set the FAILED status bit and ignores status read or write errors.
 * Reset the device before trying to initialize it again.
 *
 * @param transport The transport whose device should be marked failed. NULL is
 *                  accepted and has no effect.
 */
void
VirtioPciSetFailed(
	_In_ VirtioPciTransport_t* transport);

/**
 * @brief Reads and acknowledges the legacy INTx ISR status.
 *
 * @param transport The initialized transport.
 * @param statusOut Receives VIRTIO_ISR_* status bits.
 * @return OS_EOK on success or an argument/device I/O error.
 */
oserr_t
VirtioPciReadIsrStatus(
    _In_  VirtioPciTransport_t* transport,
    _Out_ uint8_t*              statusOut);

/**
 * @brief Allocates, programs, and enables a modern split virtqueue.
 *
 * Feature negotiation must be complete, with VIRTIO_F_RING_PACKED disabled.
 * The queue can be created before DRIVER_OK is set.
 *
 * The queue size is a power of two. A nonzero requestedSize limits the size.
 * If the requested size is zero, use the largest supported size that keeps
 * each queue component in one contiguous device allocation.
 *
 * @param transport The initialized transport controlling the device.
 * @param queueIndex The device queue index to configure.
 * @param requestedSize Preferred maximum descriptor count, or zero.
 * @param queueOut Must point to a NULL queue pointer. Receives the new queue.
 *                 If creation and cleanup both fail, receives the queue that
 *                 still needs cleanup. Destroy any non-NULL result, even when
 *                 this function returns an error.
 * @return OS_EOK on success or an allocation, configuration, or protocol error.
 */
oserr_t
VirtioSplitQueueCreate(
    _In_  VirtioPciTransport_t* transport,
    _In_  uint16_t              queueIndex,
    _In_  uint16_t              requestedSize,
    _InOut_ VirtioSplitQueue_t** queueOut);

/**
 * @brief Stop and release a split virtqueue.
 *
 * If VIRTIO_F_RING_RESET was negotiated, the queue is reset first. Otherwise,
 * an enabled queue can only be destroyed after VirtioPciReset() has completed.
 * This prevents freeing memory that the device may still access.
 *
 * @param queue The queue to destroy. NULL is accepted.
 * @return OS_EOK on success, OS_EBUSY when a device reset is required or when
 *         reset request contexts must first be reclaimed with
 *         VirtioSplitQueueAbort(), or an error encountered while resetting the
 *         queue.
 */
oserr_t
VirtioSplitQueueDestroy(
    _In_ VirtioSplitQueue_t* queue);

/**
 * @brief Builds and submits one direct descriptor chain.
 *
 * Buffer flags may contain only VIRTIO_SPLIT_DESC_F_WRITE. The function builds
 * NEXT links, and indirect descriptors are not supported yet. Once submitted,
 * the chain stays in the queue until completion or reset.
 *
 * @param queue The enabled split queue.
 * @param buffers DMA buffers to place in the descriptor chain.
 * @param bufferCount Number of buffers. Must be nonzero.
 * @param context Opaque caller context returned with the completion.
 * @param headDescriptorOut Optionally receives the descriptor-chain head.
 * @return OS_EOK when published and notified, OS_EBUSY if descriptors are
 *         unavailable, OS_EINPROGRESS if published but notification failed,
 *         or another validation/state error.
 */
oserr_t
VirtioSplitQueueSubmit(
    _In_      VirtioSplitQueue_t*       queue,
    _In_      const VirtioQueueBuffer_t* buffers,
    _In_      uint16_t                  bufferCount,
    _In_Opt_  void*                     context,
    _Out_Opt_ uint16_t*                 headDescriptorOut);

/**
 * @brief Consumes one used-ring entry and reclaims its descriptor chain.
 *
 * Returns without waiting. Once the device has finished with a chain, returns
 * the context supplied when it was submitted.
 *
 * @param queue The split queue to poll.
 * @param completionOut Receives completion information.
 * @return OS_EOK when a completion was consumed, OS_ENOENT when none is
 *         available, or a state/protocol error.
 */
oserr_t
VirtioSplitQueuePoll(
    _In_  VirtioSplitQueue_t*     queue,
    _Out_ VirtioQueueCompletion_t* completionOut);

/**
 * @brief Reclaims one outstanding chain after the queue has been reset.
 *
 * The queue must be reset first, using VIRTIO_F_RING_RESET or a completed
 * VirtioPciReset(). If queue reset is supported, VirtioSplitQueueDestroy()
 * starts it and returns OS_EBUSY while contexts remain. Abort returns the
 * caller context so the driver can cancel the request. There is no device
 * result for an aborted chain.
 *
 * @param queue The reset split queue.
 * @param completionOut Receives the cancelled chain context and head. Length
 *                      is always zero.
 * @return OS_EOK when a chain was reclaimed, OS_ENOENT when none remain,
 *         OS_EBUSY while the device can still access the queue, or a protocol
 *         error if the saved chain is inconsistent.
 */
oserr_t
VirtioSplitQueueAbort(
    _In_  VirtioSplitQueue_t*      queue,
    _Out_ VirtioQueueCompletion_t* completionOut);

/**
 * @brief Read queue counters and ring indices under the queue lock.
 *
 * @param queue The split queue to inspect.
 * @param statsOut Receives the counters and ring indices.
 * @return OS_EOK, or OS_EINVALPARAMS if either argument is NULL.
 */
oserr_t
VirtioSplitQueueGetStats(
    _In_  VirtioSplitQueue_t* queue,
    _Out_ VirtioQueueStats_t* statsOut);

_CODE_END

#endif //!__VIRTIO_VIRTIO_H__
