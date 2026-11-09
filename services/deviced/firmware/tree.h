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
 */

#ifndef __FIRMWARE_TREE_H__
#define __FIRMWARE_TREE_H__

#include <firmware/resources.h>
#include <startup.h>

/**
 * @brief Parse a device tree and build an index of its nodes.
 * The caller must keep the blob allocated and unchanged until the last tree
 * reference is released. Consumer callbacks only run after parsing succeeds.
 *
 * @param blob Device-tree bytes.
 * @param length Available bytes.
 * @param tree Receives a tree with one reference, unchanged on failure.
 * @return OS_EOK, OS_EOOM or an invalid firmware error.
 */
__EXTERN oserr_t
FdtTreeCreate(
    _In_  const void*      blob,
    _In_  size_t           length,
    _Out_ struct FdtTree** tree);

/**
 * @brief Add a reference to the parsed inventory and its property views.
 *
 * @param tree Live inventory for which the caller already holds a reference.
 */
__EXTERN void
FdtTreeAddReference(
    _In_ struct FdtTree* tree);

/**
 * @brief Release an inventory reference.
 * Frees the inventory after its final reference is released. The caller remains
 * responsible for freeing or unmapping the original blob.
 *
 * @param tree Inventory with a reference held by the caller, or NULL.
 */
__EXTERN void
FdtTreeRelease(
    _In_ struct FdtTree* tree);

/**
 * @brief Find a cached record by its structure offset in logarithmic time.
 * Includes disabled and malformed nodes. Callers must inspect their flags.
 *
 * @param tree Parsed inventory.
 * @param offset Structure-block node offset.
 * @return Record valid while the caller holds a tree reference, or NULL if the offset
 *         is not a node.
 */
__EXTERN const struct FdtResources*
FdtTreeNode(
    _In_ const struct FdtTree* tree,
    _In_ uint32_t              offset);

/**
 * @brief Return the cached parent of a node without reparsing the device tree.
 *
 * @param tree Inventory that the caller holds a reference to.
 * @param offset Structure offset of a node in tree.
 * @return Parent record valid while the caller holds a tree reference, or NULL for the
 *         root or an unknown offset.
 */
__EXTERN const struct FdtResources*
FdtTreeParent(
    _In_ const struct FdtTree* tree,
    _In_ uint32_t              offset);

/**
 * @brief Visit cached records in a subtree, including the subtree root.
 * Records are visited in firmware order, parents before children. No firmware
 * parsing occurs. Each callback receives the path from the full tree root to the
 * current node. The array is temporary. Hold a tree reference while using any
 * pointers in copied records.
 *
 * @param tree Parsed inventory.
 * @param offset Subtree root's structure offset.
 * @param visitor Consumer callback.
 * @param context Consumer state.
 * @return OS_EOK, OS_ENOENT for an unknown offset, or OS_EOOM.
 */
__EXTERN oserr_t
FdtVisitSubtree(
    _In_    const struct FdtTree* tree,
    _In_    uint32_t              offset,
    _In_    FdtResourceFn         visitor,
    _InOut_ void*                 context);

/**
 * @brief Return the resolved enabled stdout node.
 *
 * @param tree Parsed inventory.
 * @return Console record valid while the caller holds a tree reference, or NULL if
 *         unresolved.
 */
__EXTERN const struct FdtResources*
FdtTreeConsole(
    _In_ const struct FdtTree* tree);

/**
 * @brief Map and parse the service's device tree before starting bus discovery.
 * The service must serialize startup and shutdown. Add a tree reference
 * before storing descriptions that point into the firmware blob beyond discovery.
 * Terminal validation failures are remembered until FirmwareShutdown succeeds.
 * Cleanup failures always request another attempt before reporting terminal failure.
 *
 * @param result Caller-provided storage. Complete on success or absence, retry
 *               for pending work, or terminal failure.
 */
__EXTERN void
FirmwareInitialize(
    _Out_ struct DmStartupResult* result);

/**
 * @brief Read the last initialization outcome without mapping or parsing firmware.
 *
 * @param result Caller-provided storage. Retry before initialization or while
 *               failed-parse cleanup is unfinished.
 */
__EXTERN void
FirmwareGetStartupResult(
    _Out_ struct DmStartupResult* result);

/**
 * @brief Return the initialized shared tree.
 * Add a reference before using it beyond the current operation.
 *
 * @return Shared inventory, or NULL when no parsed device tree is available.
 */
__EXTERN struct FdtTree*
FirmwareGetTree(void);

/**
 * @brief Unmap and free firmware after all consumers have released it.
 * If consumers are busy or unmapping fails, the caller must retry cleanup.
 *
 * @return OS_EOK, OS_EBUSY, or the unmap error.
 */
__EXTERN oserr_t
FirmwareShutdown(void);

#endif //!__FIRMWARE_TREE_H__
