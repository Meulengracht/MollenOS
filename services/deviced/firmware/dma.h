/**
 * Copyright 2026, Philip Meulengracht
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
 * Device Tree DMA helpers for deviced firmware parsing
 * Connects deviced's parsed FdtResources and FdtPciHost records to the shared
 * DMA decoder and composer in librt/libfdt.
 */

#ifndef __FIRMWARE_DMA_H__
#define __FIRMWARE_DMA_H__

#include <dma-types.h>

struct FdtPciHost;
struct FdtResources;

/**
 * @brief Read and decode one bus's dma-ranges property from its parsed record.
 *
 * Wraps FdtDmaDecode with logging: passes the bus's raw dma-ranges bytes and
 * cell sizes, then prints the exact node offset and fault reason if decoding
 * fails. Keeps missing and empty properties distinct so callers can tell
 * "no route" apart from "1:1 pass-through".
 *
 * @param bus          Parsed bus record with raw dma-ranges bytes and cell sizes.
 * @param childFormat  Address encoding used by devices below this bus.
 * @param parentFormat Address encoding used on this bus's parent.
 * @param windows      Receives the decoded windows on success. Unchanged on error.
 * @return OS_EOK on success, OS_EINVALPARAMS for NULL arguments or invalid
 *         windows, or OS_ENOTSUPPORTED for unsupported formats or counts.
 */
oserr_t
FdtDecodeDmaRanges(
    _In_  const struct FdtResources* bus,
    _In_  enum FdtDmaAddressFormat   childFormat,
    _In_  enum FdtDmaAddressFormat   parentFormat,
    _Out_ FdtDmaBusWindows_t*        windows);

/**
 * @brief Copy a node's DMA-relevant facts into the shared FdtDmaNode_t struct.
 *
 * The Compatible pointer still refers to bytes inside the caller's parsed
 * tree, so keep that tree alive while reading @p description.
 *
 * @param node        Parsed node to inspect.
 * @param parent      Parsed parent of @p node, or NULL if @p node is the root.
 * @param description Receives the filled node summary.
 */
void
FdtDescribeDmaNode(
    _In_  const struct FdtResources* node,
    _In_  const struct FdtResources* parent,
    _Out_ FdtDmaNode_t*              description);

/**
 * @brief Extract sorted RAM DMA ranges from a parsed PCI host description.
 *
 * PCI hosts can have windows to RAM, windows to other PCI devices (peer), and
 * windows to interrupt write targets (MSI). This validates every window first
 * so overlaps between RAM and peer or MSI addresses are caught early. After
 * validation, only RAM windows are kept and sorted by device address. Exact
 * byte lengths are preserved instead of rounding up to hardware alignments, so
 * adjacent unmapped memory is never treated as usable.
 *
 * @param host Decoded PCI host record. Hardware setup is not required or checked.
 * @param map  Receives sorted RAM ranges on success. Unchanged on error.
 * @return OS_EOK on success, OS_ENOENT when no RAM window exists,
 *         OS_EINVALPARAMS for NULL arguments or overlapping/invalid windows,
 *         or OS_ENOTSUPPORTED for unsupported flags or too many windows.
 */
oserr_t
FdtPciDmaMap(
    _In_  const struct FdtPciHost* host,
    _Out_ FdtDmaMap_t*             map);

#endif
