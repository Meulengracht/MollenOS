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
 */

#ifndef __ETHERNET_MAC_H__
#define __ETHERNET_MAC_H__

#include <ddk/dma.h>

/**
 * @brief Frame storage mapped for both CPU copies and MAC DMA access.
 */
struct EthernetDma {
    // Allocation passed to DMAFree after the MAC has stopped using it.
    struct DMABuffer* Buffer;
    // CPU mapping. Valid until buffer cleanup begins.
    uint8_t*          Data;
    // Start of the contiguous device range. May differ from the physical address.
    uint64_t          Address;
};

/**
 * @brief Allocate contiguous, continuously shared frame storage.
 *
 * The device address is aligned to 64 bytes and the length is rounded to pages.
 * The MAC must issue descriptor barriers and stop DMA before freeing the buffer.
 *
 * @param context Open MAC DMA context.
 * @param length Requested bytes.
 * @param storage Zero-initialized allocation state. Buffer must be NULL.
 * @return OS_EOK on success, otherwise the DMA error with storage unchanged.
 */
__EXTERN oserr_t
EthernetDmaAllocate(
    _In_    struct DMAContext*  context,
    _In_    size_t              length,
    _InOut_ struct EthernetDma* storage);

#endif //!__ETHERNET_MAC_H__
