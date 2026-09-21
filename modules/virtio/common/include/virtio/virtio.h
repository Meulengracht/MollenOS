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

#include <ddk/busdevice.h>
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
 * The different types of Virtio PCI capabilities are as follows:
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
 * The device status field starts out as 0, and is reinitialized to 0 by 
 * the device during reset.
 * 
 * The driver MUST update device status, setting bits to indicate the 
 * completed steps of the driver initialization sequence specified in 3.1 (Of the spec). 
 * The driver MUST NOT clear a device status bit. If the driver sets the 
 * FAILED bit, the driver MUST later reset the device before attempting to 
 * re-initialize.

 * The driver SHOULD NOT rely on completion of operations of a device if 
 * DEVICE_NEEDS_RESET is set. Note: For example, the driver can’t assume 
 * requests in flight will be completed if DEVICE_NEEDS_RESET is set, nor 
 * can it assume that they have not been completed. A good implementation 
 * will try to recover by issuing a reset.
 * 
 * The device MUST NOT consume buffers or send any used buffer notifications 
 * to the driver before DRIVER_OK.

 * The device SHOULD set DEVICE_NEEDS_RESET when it enters an error state 
 * that a reset is needed. If DRIVER_OK is set, after it sets DEVICE_NEEDS_RESET, 
 * the device MUST send a device configuration change notification to the driver.
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
 * Each virtio device offers all the features it understands. 
 * During device initialization, the driver reads this and tells the 
 * device the subset that it accepts. The only way to renegotiate is to 
 * reset the device.
 *
 * This allows for forwards and backwards compatibility: 
 * if the device is enhanced with a new feature bit, older drivers will 
 * not write that feature bit back to the device. Similarly, if a driver 
 * is enhanced with a feature that the device doesn’t support, it see the 
 * new feature is not offered.
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
 * @brief Represents a Virtio PCI capability structure.
 * Each structure can be mapped by a Base Address register (BAR) 
 * belonging to the function, or accessed via the special 
 * VIRTIO_PCI_CAP_PCI_CFG field in the PCI configuration space.
 * 
 * The location of each structure is specified using a vendor-specific 
 * PCI capability located on the capability list in PCI configuration 
 * space of the device. This virtio structure capability uses little-endian 
 * format; all fields are read-only for the driver unless stated otherwise:
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
 * @brief Represents the common configuration structure for a Virtio PCI device.
 * The driver MUST NOT write to DeviceFeature, NumQueues, ConfigGeneration, QueueNotifyOff or QueueNotifyData.
 * If VIRTIO_F_RING_PACKED has been negotiated, the driver MUST NOT write the value 0 to queue_size. 
 * If VIRTIO_F_RING_PACKED has not been negotiated, the driver MUST NOT write a value which is not a power of 2 to queue_size.
 * 
 * The driver MUST configure the other virtqueue fields before enabling the virtqueue with queue_enable.
 * After writing 0 to device_status, the driver MUST wait for a read of device_status to return 0 
 * before reinitializing the device.
 *
 * The driver MUST NOT write a 0 to queue_enable.
 *
 * If VIRTIO_F_RING_RESET has been negotiated, after the driver writes 1 to queue_reset to reset the queue, 
 * the driver MUST NOT consider queue reset to be complete until it reads back 0 in queue_reset. 
 * The driver MAY re-enable the queue by writing 1 to queue_enable after ensuring that other virtqueue fields 
 * have been set up correctly. The driver MAY set driver-writeable queue configuration values to different 
 * values than those that were used before the queue reset. (see 2.6.1).
 */
PACKED_TYPESTRUCT(VirtioPciCommonConfiguration, {
    /**
     * About the whole device.
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
     * About a specific virtqueue.
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
    uint64_t Address; // Physical Address of Guest
    uint32_t Length;
    uint16_t Flags;   // Descriptor flags (VIRTIO_SPLIT_DESC_F_*)
    uint16_t Next;    // Next descriptor if Flags & VIRTIO_SPLIT_DESC_F_NEXT
});

/* Descriptor indicating that the driver should not be interrupted when a buffer is available. */
#define VIRTIO_SPLIT_AVAIL_F_NO_INTERRUPT 0x0001

/**
 * @brief Header of the driver-owned available ring.
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
 * @brief Header of the device-owned used ring.
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
 * @brief Represents the notify capability for a Virtio PCI device.
 * The notification location is found using the VIRTIO_PCI_CAP_NOTIFY_CFG capability. 
 * This capability is immediately followed by an additional field, like so:
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
 * The VIRTIO_PCI_CAP_ISR_CFG capability refers to at least a single byte, 
 * which contains the 8-bit ISR status field to be used for INT#x interrupt handling.
 * 
 * The offset for the ISR status has no alignment requirements.
 *
 * The ISR bits allow the driver to distinguish between device-specific configuration change interrupts and normal virtqueue interrupts:
 *
 * Bits     0               1                               2 to 31
 * Purpose	Queue Interrupt	Device Configuration Interrupt	Reserved
 * 
 * To avoid an extra access, simply reading this register resets it to 0 and causes the device to de-assert the interrupt.
 *
 * In this way, driver read of ISR status causes the device to de-assert an interrupt.
 * If MSI-X capability is enabled, the driver SHOULD NOT access ISR status upon detecting a Queue Interrupt.
 */

/**
 * @brief Represents the vendor-specific capability for a Virtio PCI device.
 * The driver SHOULD NOT use the Vendor data capability except for debugging and reporting purposes.
 * The driver MUST qualify the VendorId before interpreting or writing into the Vendor data capability.
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
 * @brief Represents the PCI configuration capability for a Virtio PCI device.
 * The capability is immediately followed by an additional field like so:
 * 
 * The fields cap.Bar, cap.Length, cap.Offset and PciConfiguration are read-write (RW) for the driver.
 *
 * To access a device region, the driver writes into the capability structure (ie. within the PCI configuration space) as follows:
 *   The driver sets the BAR to access by writing to cap.Bar.
 *   The driver sets the size of the access by writing 1, 2 or 4 to cap.Length.
 *   The driver sets the offset within the BAR by writing to cap.Offset.
 * 
 * At that point, PciConfiguration will provide a window of size cap.Length 
 * into the given cap.Bar at offset cap.Offset.
 * 
 * The driver MUST NOT write a cap.Offset which is not a multiple of cap.Length (ie. all accesses MUST be aligned).
 * The driver MUST NOT read or write PciConfiguration unless cap.Bar, cap.Length and cap.Offset address 
 * cap.Length bytes within a BAR range specified by some other Virtio Structure PCI Capability 
 * of type other than VIRTIO_PCI_CAP_PCI_CFG.
 */
PACKED_TYPESTRUCT(VirtioPciConfigurationCapability, {
    uint8_t PciConfiguration[4]; // Data for BAR access
});

/**
 * @brief Represents a PCI region for a Virtio device.
 *
 * A PCI region corresponds to a Base Address Register (BAR) in the PCI configuration space.
 * It contains the I/O space, offset, and length of the region.
 */
typedef struct VirtioPciRegion {
	DeviceIo_t* IoSpace;
	uint32_t    Offset;
	uint32_t    Length;
} VirtioPciRegion_t;

/**
 * @brief Represents the PCI transport for a Virtio device.
 */
typedef struct VirtioPciTransport {
	BusDevice_t*      Device;
	VirtioPciRegion_t Regions[VIRTIO_PCI_CAP_COUNT];
	uint64_t          DeviceFeatures;
	uint64_t          DriverFeatures;
	uint32_t          NotifyOffsetMultiplier;
	uint32_t          ResetGeneration;
	uint16_t          ActiveQueues;
	uint8_t           AcquiredBars;
	spinlock_t        ConfigurationLock;
} VirtioPciTransport_t;

typedef struct VirtioSplitQueue VirtioSplitQueue_t;

/**
 * @brief One physically contiguous buffer referenced by a descriptor.
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
 * @brief Snapshot of split virtqueue accounting state.
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
 * Discovers the Virtio vendor capabilities, validates their BAR regions,
 * acquires each referenced BAR, and enables PCI memory access and bus
 * mastering. This function does not reset the device or negotiate features.
 *
 * The caller retains ownership of @p device and must keep it alive until
 * VirtioPciTransportDestroy() has returned. On failure, any resources acquired
 * by this function are released and @p transport is cleared.
 *
 * @param device The PCI bus device to initialize. Its vendor must be
 *               VIRTIO_PCI_VENDOR_ID.
 * @param transport Receives the initialized transport state.
 * @return OS_EOK on success, OS_EINVALPARAMS for invalid arguments or a
 *         non-Virtio vendor, OS_ENOTSUPPORTED if the required modern PCI
 *         capabilities are absent, or another error from PCI configuration,
 *         BAR acquisition, or bus control.
 */
oserr_t
VirtioPciTransportInitialize(
	_In_  BusDevice_t*          device,
	_Out_ VirtioPciTransport_t* transport);

/**
 * @brief Releases resources held by a Virtio PCI transport.
 *
 * Releases every BAR acquired during initialization and clears the transport
 * structure. The referenced BusDevice_t is not freed. It is safe to call this
 * function with NULL or with a cleared transport.
 *
 * @param transport The transport to destroy.
 */
void
VirtioPciTransportDestroy(
	_In_ VirtioPciTransport_t* transport);

/**
 * @brief Reads a value from a mapped Virtio PCI capability region.
 *
 * @param region The capability region to access.
 * @param offset Byte offset relative to the beginning of the region.
 * @param width Access width in bytes; must be 1, 2, 4, or 8.
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
 * @param width Access width in bytes; must be 1, 2, 4, or 8.
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
 * Writes zero to the device status field, then yields while waiting for the
 * device to read back a zero status. The transport must have been initialized.
 * All previously negotiated features and device configuration are invalidated.
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
 * Performs the ACKNOWLEDGE, DRIVER, and FEATURES_OK initialization stages.
 * VIRTIO_F_VERSION_1 is automatically added to both the supported and required
 * sets. The negotiated set is the intersection of device and driver-supported
 * features. Every required feature must be offered by the device.
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
 * Sets DRIVER_OK and verifies that the device accepted the state without
 * reporting FAILED or DEVICE_NEEDS_RESET. Call this only after successful
 * feature negotiation and all required queue and device-specific setup.
 *
 * @param transport The initialized and configured transport.
 * @return OS_EOK when DRIVER_OK is accepted, OS_EINVALPARAMS if @p transport
 *         is NULL, OS_EDEVFAULT if the resulting device status is invalid, or
 *         an I/O error from the common configuration region.
 */
oserr_t
VirtioPciFinishInitialization(
	_In_ VirtioPciTransport_t* transport);

/**
 * @brief Marks Virtio device initialization or operation as failed.
 *
 * Sets the FAILED status bit on a best-effort basis. Any status read or write
 * error is intentionally ignored. The device must be reset before another
 * initialization attempt.
 *
 * @param transport The transport whose device should be marked failed. NULL is
 *                  accepted and has no effect.
 */
void
VirtioPciSetFailed(
	_In_ VirtioPciTransport_t* transport);

/**
 * @brief Reads and acknowledges the legacy INTx ISR status.
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
 * The requested size is treated as an upper bound. Zero selects the largest
 * supported size that keeps each queue component in one physically contiguous
 * allocation. The resulting size is a power of two.
 *
 * Feature negotiation must have completed, and VIRTIO_F_RING_PACKED must not
 * be negotiated. The queue may be created before DRIVER_OK is set.
 *
 * @param transport The initialized transport controlling the device.
 * @param queueIndex The device queue index to configure.
 * @param requestedSize Preferred maximum descriptor count, or zero.
 * @param queueOut Receives the allocated queue.
 * @return OS_EOK on success or an allocation, configuration, or protocol error.
 */
oserr_t
VirtioSplitQueueCreate(
    _In_  VirtioPciTransport_t* transport,
    _In_  uint16_t              queueIndex,
    _In_  uint16_t              requestedSize,
    _Out_ VirtioSplitQueue_t**  queueOut);

/**
 * @brief Safely releases a split virtqueue.
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
 * Only VIRTIO_SPLIT_DESC_F_WRITE is accepted in buffer flags; NEXT links are
 * generated internally and indirect descriptors are not supported yet. Once
 * published, the chain remains owned by the queue until completion or reset.
 *
 * @param queue The enabled split queue.
 * @param buffers DMA buffers to place in the descriptor chain.
 * @param bufferCount Number of buffers; must be nonzero.
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
 * This operation is nonblocking. The opaque context supplied during submission
 * is returned after the device relinquishes the chain.
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
 * The queue must first have been reset through VIRTIO_F_RING_RESET or by a
 * completed VirtioPciReset(). VirtioSplitQueueDestroy() initiates the former
 * when available and returns OS_EBUSY while contexts remain. This operation
 * never reports a device result; it only returns the caller context so the
 * owning driver can cancel it.
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
 * @brief Retrieves a consistent snapshot of queue accounting state.
 */
oserr_t
VirtioSplitQueueGetStats(
    _In_  VirtioSplitQueue_t* queue,
    _Out_ VirtioQueueStats_t* statsOut);

_CODE_END

#endif //!__VIRTIO_VIRTIO_H__
