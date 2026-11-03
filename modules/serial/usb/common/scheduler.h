/**
 * Copyright 2018, Philip Meulengracht
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
 * USB Controller Scheduler
 * - Shared controller scheduler for the USB drivers.
 */

#ifndef __USB_SCHEDULER__
#define __USB_SCHEDULER__

#include <ddk/ddkdefs.h>
#include <ddk/dma.h>
#include <os/spinlock.h>

#define FRAME_TIME_USECS                1000L
#define FRAME_TIME_BITS                 12000L
#define FRAME_TIME_MAX_BITS_ALLOC       (90L * FRAME_TIME_BITS / 100L)
#define FRAME_TIME_MAX_USECS_ALLOC      (90L * FRAME_TIME_USECS / 100L)

#define BitTime(_length) (7 * 8 * ((long)(_length)) / 6)
#define NS_TO_US(ns)     DIVUP(ns, 1000L)

/* Full/low speed bandwidth allocation constants/support. */
#define BW_HOST_DELAY   1000L
#define BW_HUB_LS_SETUP 333L

/* Ceiling [nano/micro]seconds (typical) for that many bytes at high speed
 * ISO is a bit less, no ACK ... from USB 2.0 spec, 5.11.3 (and needed
 * to preallocate bandwidth) */
#define USB2_HOST_DELAY 5       // nsec, guess
#define HS_NSECS(bytes) (((55 * 8 * 2083) \
        + (2083UL * (3 + BitTime(bytes))))/1000 \
        + USB2_HOST_DELAY)
#define HS_NSECS_ISO(bytes) (((38 * 8 * 2083) \
        + (2083UL * (3 + BitTime(bytes))))/1000 \
        + USB2_HOST_DELAY)
#define HS_USECS(bytes)         NS_TO_US(HS_NSECS(bytes))
#define HS_USECS_ISO(bytes)     NS_TO_US(HS_NSECS_ISO(bytes))

/**
 * @brief Common metadata for objects scheduled by a USB controller.
 * Stores chain links, allocation flags and periodic bandwidth information.
 */
PACKED_TYPESTRUCT(UsbSchedulerObject, {
    uint32_t Flags;
    uint16_t Index;
    uint16_t BreathIndex;
    uint16_t DepthIndex;
    uint16_t FrameInterval;
    uint16_t Bandwidth;
    uint16_t StartFrame;
    uint16_t FrameMask;
});

/* UsbSchedulerObject::Flags
 * Contains definitions and bitfield definitions for UsbSchedulerObject::Flags 
 * Bit  0-11: Reserved and should be always set to link-flags for the element. 
 * Bit    12: Allocation status
 * Bit    13: Has bandwidth allocated
 * Bit    14: Isochronous Element
 * Bit    15: Processed
 * Bit 16-31: Available */
#define USB_ELEMENT_LINKFLAGS(_flags)   ((_flags) & 0xFFF)
#define USB_ELEMENT_ALLOCATED           (1 << 12)
#define USB_ELEMENT_BANDWIDTH           (1 << 13)
#define USB_ELEMENT_ISOCHRONOUS         (1 << 14)
#define USB_ELEMENT_PROCESSED           (1 << 15)

#define USB_ELEMENT_INDEX_MASK          0x1FFF
#define USB_ELEMENT_POOL_MASK           0x7
#define USB_ELEMENT_POOL_SHIFT          13

#define USB_ELEMENT_CREATE_INDEX(_pool, _idx) (uint16_t)(((_idx) & USB_ELEMENT_INDEX_MASK) | (((_pool) & USB_ELEMENT_POOL_MASK) << USB_ELEMENT_POOL_SHIFT))
#define USB_ELEMENT_NO_INDEX                  (uint16_t)0xFFFF

#define USB_ELEMENT_LINK_END            (1 << 0)

#define USB_CHAIN_BREATH                0
#define USB_CHAIN_DEPTH                 1
#define USB_POOL_MAXCOUNT               8

/**
 * @brief Element layout, link offsets and storage for one scheduler pool.
 */
typedef struct UsbSchedulerPool {
    size_t    ElementBaseSize;            // Size of an element
    size_t    ElementAlignedSize;         // Size of an element
    size_t    ElementCount;               // Number of elements
    size_t    ElementCountReserved;       // Number of reserved elements

    size_t ElementLinkBreathOffset;    // Offset to the physical breath link member
    size_t ElementLinkDepthOffset;     // Offset to the physical depth link member
    size_t ElementObjectOffset;        // Offset to the UsbSchedulerObject
    
    // Hardware pools use one contiguous DMA buffer. Software pools use aligned heap memory.
    struct DMABuffer* ElementPoolDMA;
    uintptr_t         ElementPoolDeviceAddress;
    uint8_t*          ElementPool;
} UsbSchedulerPool_t;

/**
 * @brief Controller frame limits, DMA context and element pool configuration.
 */
typedef struct UsbSchedulerSettings {
    unsigned int       Flags;                // Flags
    size_t             FrameCount;           // Number of frames
    size_t             SubframeCount;        // Number of sub-frames
    size_t             MaxBandwidthPerFrame; // Max bandwidth per frame
    struct DMAContext* DmaContext;           // Not managed by the scheduler
    struct DMABuffer*  FrameListDMA;
    reg32_t*           FrameList;            // Physical frame list
    uintptr_t          FrameListPhysical;

    int                PoolCount;               // Number of pools in use
    UsbSchedulerPool_t Pools[USB_POOL_MAXCOUNT];// Pools
} UsbSchedulerSettings_t;

#define USB_SCHEDULER_FRAMELIST         (1 << 0) // If set, we should create a framelist
#define USB_SCHEDULER_FL64              (1 << 1) // If set, use the 64 bit framelist
#define USB_SCHEDULER_NULL_ELEMENT      (1 << 2) // If set, all chains make use of null-elements
#define USB_SCHEDULER_DEFERRED_CLEAN    (1 << 3) // If set, cleanup must occur later than unlink
#define USB_SCHEDULER_LINK_BIT_EOL      (1 << 4) // Specify that empty links must be marked with EOL
#define USB_SCHEDULER_PERIODIC          (1 << 5) // If set, enable periodic/bandwidth bookkeeping
// Element pools contain CPU-only metadata and have no physical address table.
// Hardware rings and frame lists must use their own DMA allocations.
#define USB_SCHEDULER_SOFTWARE_ELEMENTS (1 << 6)

typedef struct UsbTransferArena {
    UsbSchedulerSettings_t* Settings;
    spinlock_t*             Lock;
} UsbTransferArena_t;

/**
 * @brief Virtual frame links and bandwidth allocated to the periodic schedule.
 */
typedef struct UsbPeriodicScheduler {
    UsbSchedulerSettings_t* Settings;
    uintptr_t* VirtualFrameList;       // Virtual frame list
    size_t*    Bandwidth;              // Bandwidth[FrameCount]
    size_t     TotalBandwidth;         // Total bandwidth
} UsbPeriodicScheduler_t;

/**
 * @brief Shared settings and lock for element pools and periodic scheduling.
 */
typedef struct UsbScheduler {
    UsbSchedulerSettings_t Settings;
    spinlock_t             Lock;

    UsbTransferArena_t     TransferArena;
    UsbPeriodicScheduler_t Periodic;
} UsbScheduler_t;

#define USB_ELEMENT_INDEX(_pool, _index)     ((uint8_t*)&((_pool)->ElementPool[((_index) & USB_ELEMENT_INDEX_MASK) * (_pool)->ElementAlignedSize]))
#define USB_ELEMENT_PHYSICAL(_pool, _index)  (UsbSchedulerGetDma((_pool), USB_ELEMENT_INDEX((_pool), (_index))))
#define USB_ELEMENT_GET_POOL(_sched, _index) (&(_sched)->Settings.Pools[((_index) >> USB_ELEMENT_POOL_SHIFT) & USB_ELEMENT_POOL_MASK])

/**
 * @brief Access the next link's 32-bit physical address.
 * USB elements can have a DEPTH link, a BREATH link, or both.
 * _dir must be USB_CHAIN_BREATH or USB_CHAIN_DEPTH.
 */
#define __LINK_OFFSET(_pool, _dir) (((_dir) == USB_CHAIN_BREATH) ? (_pool)->ElementLinkBreathOffset : (_pool)->ElementLinkDepthOffset)
#define USB_ELEMENT_LINK(_pool, _elem, _dir) (*(reg32_t*)((uint8_t*)(_elem) + __LINK_OFFSET(_pool, _dir)))

/**
 * @brief Get the UsbSchedulerObject_t following the element's hardware data.
 * Each transfer descriptor has this metadata for the common USB scheduler.
 */
#define USB_ELEMENT_OBJECT(_pool, _elem) ((UsbSchedulerObject_t*)((uint8_t*)(_elem) + (_pool)->ElementObjectOffset))

static inline UsbTransferArena_t*
UsbSchedulerGetTransferArena(
    _In_ UsbScheduler_t* Scheduler)
{
    return &Scheduler->TransferArena;
}

static inline UsbPeriodicScheduler_t*
UsbSchedulerGetPeriodicScheduler(
    _In_ UsbScheduler_t* Scheduler)
{
    return &Scheduler->Periodic;
}

static inline bool
UsbSchedulerHasPeriodicSchedule(
    _In_ UsbScheduler_t* Scheduler)
{
    return (Scheduler->Settings.Flags & USB_SCHEDULER_PERIODIC) != 0;
}

/**
 * @brief Initialize scheduler settings.
 */
extern void
UsbSchedulerSettingsCreate(
    _In_ UsbSchedulerSettings_t* Settings,
    _In_ size_t                  FrameCount,
    _In_ size_t                  SubframeCount,
    _In_ size_t                  MaxBandwidthPerFrame,
    _In_ unsigned int                 Flags);

/**
 * @brief Configure the frame list supplied by the controller.
 * Call this before initialization if the controller supplies its own frame list.
 */
extern void
UsbSchedulerSettingsConfigureFrameList(
    _In_ UsbSchedulerSettings_t*    Settings,
    _In_ reg32_t*                   FrameList,
    _In_ uintptr_t                  FrameListPhysical);

/**
 * @brief Add a pool to be created during scheduler initialization.
 */
extern void
UsbSchedulerSettingsAddPool(
    _In_ UsbSchedulerSettings_t*    Settings,
    _In_ size_t                     ElementSize,
    _In_ size_t                     ElementAlignment,
    _In_ size_t                     ElementCount,
    _In_ size_t                     ElementCountReserved,
    _In_ size_t                     LinkBreathMemberOffset,
    _In_ size_t                     LinkDepthMemberOffset,
    _In_ size_t                     ObjectMemberOffset);

/**
 * @brief Allocate descriptor pools and optional frame-list storage.
 *
 * @param Settings Pool sizes and layout, and the controller DMA context.
 * Software-only pools do not need a DMA context.
 * @param SchedulerOut Receives the scheduler before its resources are allocated.
 * If initialization fails with a non-NULL result, pass it to UsbSchedulerDestroy
 * and retry destruction until cleanup succeeds.
 * @return OS_EOK on success, otherwise an allocation or initialization error.
 */
extern oserr_t
UsbSchedulerInitialize(
    _In_  UsbSchedulerSettings_t* Settings,
    _Out_ UsbScheduler_t**        SchedulerOut);

/**
 * @brief Release scheduler resources. Hardware must have stopped using the descriptors.
 *
 * @param scheduler Pointer to the scheduler, set to NULL on success. If cleanup
 * fails, the scheduler still tracks the remaining resources so the call can be retried.
 * @return OS_EOK on success, otherwise the DMA cleanup error.
 */
extern oserr_t
UsbSchedulerDestroy(
    _InOut_ UsbScheduler_t** scheduler);

/**
 * @brief Reset the scheduler's internal data.
 * The controller must be stopped before this call because it changes the frame list.
 */
extern oserr_t
UsbSchedulerResetInternalData(
    _In_ UsbScheduler_t*            Scheduler,
    _In_ int                        ResetElements,
    _In_ int                        ResetFramelist);

extern oserr_t
UsbTransferArenaResetInternalData(
    _In_ UsbTransferArena_t* arena,
    _In_ int                 resetElements);

extern oserr_t
UsbPeriodicSchedulerResetInternalData(
    _In_ UsbPeriodicScheduler_t* periodicScheduler,
    _In_ int                     resetFramelist);

extern uintptr_t
UsbTransferArenaGetDma(
    _In_ UsbSchedulerPool_t* arenaPool,
    _In_ const uint8_t*      elementPointer);

extern oserr_t
UsbTransferArenaGetPoolElement(
    _In_  UsbTransferArena_t* arena,
    _In_  int                 Pool,
    _In_  int                 Index,
    _Out_ uint8_t**           ElementOut,
    _Out_ uintptr_t*          ElementPhysicalOut);

extern oserr_t
UsbTransferArenaGetPoolFromElement(
    _In_  UsbTransferArena_t*  arena,
    _In_  const uint8_t*       element,
    _Out_ UsbSchedulerPool_t** poolOut);

extern oserr_t
UsbTransferArenaAllocateElement(
    _In_  UsbTransferArena_t* arena,
    _In_  int                 Pool,
    _Out_ uint8_t**           ElementOut);

extern void
UsbTransferArenaFreeElement(
    _In_ UsbTransferArena_t* arena,
    _In_ uint8_t*            element);

extern oserr_t
UsbTransferArenaChainElement(
    _In_ UsbTransferArena_t* arena,
    _In_ int                 ElementRootPool,
    _In_ uint8_t*            ElementRoot,
    _In_ int                 ElementPool,
    _In_ uint8_t*            Element,
    _In_ uint16_t            Marker,
    _In_ int                 Direction);

extern oserr_t
UsbTransferArenaUnchainElement(
    _In_ UsbTransferArena_t* arena,
    _In_ int                 ElementRootPool,
    _In_ uint8_t*            ElementRoot,
    _In_ int                 ElementPool,
    _In_ uint8_t*            Element,
    _In_ int                 Direction);

extern oserr_t
UsbPeriodicSchedulerAllocateBandwidth(
    _In_ UsbTransferArena_t*    arena,
    _In_ UsbPeriodicScheduler_t* periodicScheduler,
    _In_ uint8_t                interval,
    _In_ uint16_t               mps,
    _In_ uint8_t                transactionType,
    _In_ size_t                 bytesToTransfer,
    _In_ uint8_t                transferType,
    _In_ uint8_t                speed,
    _In_ uint8_t*               element);

extern oserr_t
UsbPeriodicSchedulerLinkElement(
    _In_ UsbTransferArena_t*     arena,
    _In_ UsbPeriodicScheduler_t* periodicScheduler,
    _In_ int                     ElementPool,
    _In_ uint8_t*                Element);

extern void
UsbPeriodicSchedulerUnlinkElement(
    _In_ UsbTransferArena_t*     arena,
    _In_ UsbPeriodicScheduler_t* periodicScheduler,
    _In_ int                     ElementPool,
    _In_ uint8_t*                Element);

extern uintptr_t
UsbSchedulerGetDma(
    _In_ UsbSchedulerPool_t* schedulerPool,
    _In_ const uint8_t*      elementPointer);

/**
 * @brief Get the element at the given pool and index.
 */
extern oserr_t
UsbSchedulerGetPoolElement(
    _In_  UsbScheduler_t*           Scheduler,
    _In_  int                       Pool,
    _In_  int                       Index,
    _Out_ uint8_t**                 ElementOut,
    _Out_ uintptr_t*                ElementPhysicalOut);

/**
 * @brief Find the pool containing the element address.
 */
extern oserr_t
UsbSchedulerGetPoolFromElement(
        _In_  UsbScheduler_t*      scheduler,
        _In_  const uint8_t*       element,
        _Out_ UsbSchedulerPool_t** poolOut);

/**
 * @brief Allocate an element from the scheduler pool.
 * If the pool is exhausted, returns OS_EUNKNOWN and sets ElementOut to
 * USB_OUT_OF_RESOURCES. Wait until the next transfer before retrying.
 */
extern oserr_t
UsbSchedulerAllocateElement(
    _In_  UsbScheduler_t*           Scheduler,
    _In_  int                       Pool,
    _Out_ uint8_t**                 ElementOut);

/**
 * @brief Reset an allocated element and free its associated bandwidth.
 */
extern void
UsbSchedulerFreeElement(
    _In_ UsbScheduler_t* usbScheduler,
    _In_ uint8_t*        element);

/**
 * @brief Allocate bandwidth for an element and select its place in the schedule.
 * Returns OS_EUNKNOWN if the schedule has no room for the requested bandwidth.
 */
extern oserr_t
UsbSchedulerAllocateBandwidth(
    _In_ UsbScheduler_t* scheduler,
    _In_ uint8_t         interval,
    _In_ uint16_t        mps,
    _In_ uint8_t         transactionType,
    _In_ size_t          bytesToTransfer,
    _In_ uint8_t         transferType,
    _In_ uint8_t         speed,
    _In_ uint8_t*        element);

/**
 * @brief Calculate the controller-independent periodic bandwidth estimate for a
 * transfer. Split transactions use the estimate for both their start and
 * complete phases, which may be placed in different microframes by EHCI.
 *
 * @param speed The USB bus speed of the endpoint.
 * @param direction The transfer direction used by the USB timing model.
 * @param transferType The USB transfer type used by the timing model.
 * @param bytesToTransfer The number of payload bytes represented by the slot.
 * @return The estimated periodic bandwidth cost in microseconds.
 */
extern size_t
UsbSchedulerCalculateBandwidth(
    _In_ uint8_t speed,
    _In_ uint8_t direction,
    _In_ uint8_t transferType,
    _In_ size_t  bytesToTransfer);

/**
 * @brief Reserve start-split and complete-split microframes atomically for a
 * full/low-speed periodic transfer in an EHCI split schedule. Other controllers
 * use the original bandwidth allocator.
 * OUT splits carry data in one or more consecutive start-split microframes
 * without a complete-split. IN splits use one start-split followed by a
 * complete-split window.
 *
 * @param scheduler The periodic scheduler containing the bandwidth table.
 * @param interval The requested periodic interval.
 * @param startBandwidth The bandwidth cost of each start-split slot.
 * @param completeBandwidth The bandwidth cost of each complete-split slot.
 * @param startSplitCount The number of consecutive start-split microframes
 * required (>1 only for OUT transfers whose payload spans multiple 188-byte
 * bus transactions).
 * @param needsCompleteSplit Whether a complete-split window must also be
 * reserved (true for IN, false for OUT).
 * @param element The scheduler element receiving the reservation metadata.
 * @param startMaskOut Receives the selected start-split microframe mask.
 * @param completeMaskOut Receives the selected complete-split microframe mask.
 * @param completionFrameOffsetOut Receives the frame offset for completion.
 * @return OS_EOK when both phases were reserved atomically.
 */
extern oserr_t
UsbSchedulerAllocateSplitBandwidth(
    _In_  UsbScheduler_t* scheduler,
    _In_  uint8_t         interval,
    _In_  size_t          startBandwidth,
    _In_  size_t          completeBandwidth,
    _In_  uint8_t         startSplitCount,
    _In_  bool            needsCompleteSplit,
    _In_  uint8_t*        element,
    _Out_ uint8_t*        startMaskOut,
    _Out_ uint8_t*        completeMaskOut,
    _Out_ uint8_t*        completionFrameOffsetOut);

/**
 * @brief Release the two-phase reservation returned by
 * UsbSchedulerAllocateSplitBandwidth. The masks and costs must match the
 * values used for the allocation.
 *
 * @param scheduler The periodic scheduler containing the bandwidth table.
 * @param element The scheduler element whose reservation is being released.
 * @param startMask The previously selected start-split mask.
 * @param completeMask The previously selected complete-split mask.
 * @param completionFrameOffset The previously selected frame offset.
 * @param startBandwidth The reserved start-split cost.
 * @param completeBandwidth The reserved complete-split cost.
 */
extern void
UsbSchedulerFreeSplitBandwidth(
    _In_ UsbScheduler_t* scheduler,
    _In_ uint8_t*        element,
    _In_ uint8_t         startMask,
    _In_ uint8_t         completeMask,
    _In_ uint8_t         completionFrameOffset,
    _In_ size_t          startBandwidth,
    _In_ size_t          completeBandwidth);

/**
 * @brief Add an element to a chain.
 * Specify the root element, the element to add and the chain direction.
 */
extern oserr_t
UsbSchedulerChainElement(
    _In_ UsbScheduler_t*        Scheduler,
    _In_ int                    ElementRootPool,
    _In_ uint8_t*               ElementRoot,
    _In_ int                    ElementPool,
    _In_ uint8_t*               Element,
    _In_ uint16_t               Marker,
    _In_ int                    Direction);

/**
 * @brief Remove an element from a chain.
 * Specify the root element, the element to remove and the chain direction.
 */
extern oserr_t
UsbSchedulerUnchainElement(
    _In_ UsbScheduler_t*        Scheduler,
    _In_ int                    ElementRootPool,
    _In_ uint8_t*               ElementRoot,
    _In_ int                    ElementPool,
    _In_ uint8_t*               Element,
    _In_ int                    Direction);

/**
 * @brief Queue a periodic/isochronous transfer.
 * Returns OS_EUNKNOWN if the requested bandwidth cannot be scheduled.
 */
extern oserr_t
UsbSchedulerLinkPeriodicElement(
    _In_ UsbScheduler_t*        Scheduler,
    _In_ int                    ElementPool,
    _In_ uint8_t*               Element);

/**
 * @brief Remove a queued periodic transfer (interrupt/isoc) from the controller schedule.
 */
extern void
UsbSchedulerUnlinkPeriodicElement(
    _In_ UsbScheduler_t*        Scheduler,
    _In_ int                    ElementPool,
    _In_ uint8_t*               Element);

#endif //!__USB_SCHEDULER__
