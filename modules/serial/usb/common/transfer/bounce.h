/**
 * MollenOS
 *
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
 */

#ifndef __USB_TRANSFER_BOUNCE_H__
#define __USB_TRANSFER_BOUNCE_H__

#include <ddk/dma.h>

#define USB_BOUNCE_SLOT_SIZE 64U

DECL_STRUCT(UsbManagerTransfer);
DECL_STRUCT(UsbTransferBouncePool);
DECL_STRUCT(UsbTransferPacketBounce);

/**
 * @brief Allocate a shared DMA buffer divided into 64-byte packet slots.
 * Requires *poolOut to be NULL. If creation fails with a non-NULL output,
 * the caller must destroy the partially created pool.
 *
 * @param context Controller DMA context. Must remain valid until the pool is freed.
 * @param slots   Number of packet slots to allocate.
 * @param poolOut Receives the pool, including on failure if cleanup is needed.
 * @return        OS_EOK or a validation or allocation error.
 */
oserr_t
UsbTransferBouncePoolCreate(
    _In_    struct DMAContext*        context,
    _In_    uint32_t                  slots,
    _InOut_ UsbTransferBouncePool_t** poolOut);

/**
 * @brief Free the pool after all transfers using it have been destroyed.
 * Requires new transfers to be blocked before calling.
 *
 * @param pool Pointer to the pool. NULL is allowed for *pool. Cleared on success.
 * @return     OS_EOK, OS_EBUSY if transfers still use the pool, or a DMA error.
 */
oserr_t
UsbTransferBouncePoolDestroy(
    _InOut_ UsbTransferBouncePool_t** pool);

/**
 * @brief Set up bounce state for elements marked with NeedsBounce.
 * Requires the element list and DMA mapping to be ready and unchanged during
 * this call. Slots are reserved later by UsbTransferBounceReserve.
 *
 * @param transfer Inactive transfer to prepare.
 * @return         OS_EOK or a setup error. On failure, destroy the partial state.
 */
oserr_t
UsbTransferBounceInitialize(
    _InOut_ UsbManagerTransfer_t* transfer);

/**
 * @brief Reserve all packet slots before allocating hardware descriptors.
 * Requires calls for this transfer to run serially. The transfer must be inactive
 * unless its slots are already reserved for the current TD batch.
 *
 * @param transfer Transfer whose packets need slots.
 * @return         OS_EOK, OS_EBUSY while waiting for slots, or an allocation error.
 *                 On OS_EBUSY, leave the transfer queued without descriptors.
 */
oserr_t
UsbTransferBounceReserve(
    _InOut_ UsbManagerTransfer_t* transfer);

/**
 * @brief Check whether a transfer can retry reserving packet slots.
 * Leaves the slots and wait queue unchanged.
 *
 * @param transfer Transfer checked by the controller dispatcher.
 * @return         true if the transfer is not waiting, or is first in the queue
 *                 with enough free slots.
 */
bool
UsbTransferBounceCanRetry(
    _In_ UsbManagerTransfer_t* transfer);

/**
 * @brief Copy outgoing packets into their bounce slots before starting DMA.
 * Requires all slots to be reserved and DMA to be inactive. If ending CPU
 * access fails, the next call retries it without copying the packets again.
 *
 * @param transfer Transfer to prepare for DMA.
 * @return         OS_EOK or a slot or CPU access error. The call can be retried.
 */
oserr_t
UsbTransferBouncePrepare(
    _InOut_ UsbManagerTransfer_t* transfer);

/**
 * @brief Copy received bytes back to the client buffer after DMA has ended.
 * Requires DMA completion to have succeeded. Call before notifying the client.
 * A retry continues cleanup without repeating completed copies.
 *
 * @param transfer Completed or stopped transfer.
 * @param bytes    Completed byte count from the start of the DMA range, including SETUP.
 * @return         OS_EOK or a CPU access error.
 */
oserr_t
UsbTransferBounceComplete(
    _InOut_ UsbManagerTransfer_t* transfer,
    _In_    size_t                bytes);

/**
 * @brief Return packet slots if the first submission failed before starting DMA.
 * Ends any pending CPU access before returning the slots.
 *
 * @param transfer Inactive transfer to remove from the wait queue or release slots for.
 * @return         OS_EOK or a CPU access error. On failure, the slots stay reserved.
 */
oserr_t
UsbTransferBounceAbort(
    _InOut_ UsbManagerTransfer_t* transfer);

/**
 * @brief Remove the transfer from the wait queue and free its packet resources.
 * Requires hardware access to the bounce memory to have ended. If cleanup fails,
 * the remaining resources stay in place for the next attempt.
 *
 * @param transfer Transfer to clean up.
 * @return         OS_EOK or a cleanup error. Clears Dma.Packets on success.
 */
oserr_t
UsbTransferBounceDestroy(
    _InOut_ UsbManagerTransfer_t* transfer);

#endif
