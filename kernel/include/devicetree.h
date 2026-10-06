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

#ifndef __DEVICETREE_H__
#define __DEVICETREE_H__

#include <os/osdefs.h>

#include <fdt/reader.h>

/**
 * @brief One named property attached to a device-tree node.
 *
 * The tree owns this descriptor. Name and Value point into the tree's retained
 * FDT blob and remain valid only while that tree remains alive. Value contains
 * the original property bytes, which are generally big-endian; use the
 * DeviceTreeRead* helpers for decoded values instead of casting Value to native
 * integer pointers. Properties are linked newest-first in the current parser.
 */
typedef struct DeviceTreeProperty {
    const char*                Name;   // Null-terminated property name borrowed from the retained FDT blob.
    const void*                Value;  // Raw property bytes borrowed from the retained FDT blob.
    uint32_t                   Length; // Number of value bytes.
    struct DeviceTreeProperty* Next;   // Next property on this node, or NULL at the end of the list.
} DeviceTreeProperty_t;

/**
 * @brief One node in the retained device tree.
 *
 * The tree owns each node descriptor. Names and properties refer to data in the
 * tree's retained FDT blob, and all node pointers are valid only for the
 * lifetime of that tree. Phandle is decoded to host byte order; it is zero if
 * the node has no phandle. Unknown properties and nodes are retained so callers
 * can interpret bindings not handled by the generic helpers.
 */
typedef struct DeviceTreeNode {
    const char*            Name;      // Node name, borrowed from the retained FDT blob.
    uint32_t               Phandle;   // Host-endian phandle, or zero when none is defined.
    struct DeviceTreeNode* Parent;    // Parent node, or NULL for the root.
    struct DeviceTreeNode* Children;  // First child node, or NULL when there are no children.
    struct DeviceTreeNode* Next;      // Next sibling under the same parent.
    struct DeviceTreeNode* NextNode;  // Next node in the tree-wide flat list.
    DeviceTreeProperty_t*  Properties; // Head of this node's property list.
} DeviceTreeNode_t;

/**
 * @brief An owned, parsed snapshot of a flattened device tree.
 *
 * DeviceTreeCreate copies the validated input blob and builds node and property
 * descriptors that refer into that copy. Destroying the tree releases the blob
 * and descriptors together; pointers obtained from the tree must not be used
 * afterward. The parsed tree retains unknown bindings for platform or device
 * drivers to interpret.
 */
typedef struct DeviceTree {
    void*             Blob;    // Owned copy of the complete FDT blob.
    uint32_t          Size;    // Size of the retained blob in bytes.
    uint32_t          Version; // FDT format version from the validated header.
    DeviceTreeNode_t* Root;    // Root node of the hierarchy.
    DeviceTreeNode_t* Nodes;   // Head of the flat list containing every parsed node.
} DeviceTree_t;

/**
 * @brief A resolved provider reference and its decoded argument cells.
 *
 * This is the result of reading one entry from a phandle-based property. The
 * provider pointer is borrowed from the tree, and Cells are converted from
 * big-endian to host-endian values. The result is valid only while its tree is
 * alive; at most 16 argument cells are supported.
 */
typedef struct DeviceTreeReference {
    const DeviceTreeNode_t* Provider;   // Node identified by the entry's phandle.
    uint32_t                CellCount;  // Number of valid argument cells in Cells.
    uint32_t                Cells[16];  // Decoded provider-specific arguments; entries after CellCount are unused.
} DeviceTreeReference_t;

/**
 * @brief Validate and parse a complete FDT blob into an owned tree snapshot.
 *
 * Checks the FDT header and structure, copies the blob, and builds node and
 * property descriptors. It does not discover or register platform components.
 * On success, *treeOut receives the new tree, which the caller must eventually
 * release with DeviceTreeDestroy. On failure, *treeOut is set to NULL and any
 * partially allocated tree is freed.
 *
 * @param blob Start of the FDT blob to parse.
 * @param size Number of readable bytes in the input blob.
 * @param treeOut Receives the created tree on success, or NULL on failure.
 */
__EXTERN oserr_t
DeviceTreeCreate(
    _In_  const void*    blob,
    _In_  uint32_t       size,
    _Out_ DeviceTree_t** treeOut);

/**
 * @brief Free a tree created by DeviceTreeCreate and all data it owns.
 *
 * Releases its node and property descriptors and its private copy of the FDT
 * blob. All pointers into the tree become invalid. Do not call this on a tree
 * that is still published or otherwise in use.
 *
 * @param tree Tree to release.
 */
__EXTERN void
DeviceTreeDestroy(
    _In_ DeviceTree_t* tree);

/**
 * @brief Find a property on one node by its exact name.
 * @return A borrowed property descriptor, or NULL if the node has no such property.
 */
__EXTERN const DeviceTreeProperty_t*
DeviceTreeGetProperty(
    _In_ const DeviceTreeNode_t* node,
    _In_ const char*             name);

/**
 * @brief Read a property's value as a null-terminated string.
 *
 * Returns the property's value bytes when they contain a NUL terminator within
 * the recorded length. The returned pointer is borrowed from the tree's blob;
 * it remains valid only while the tree is alive. Returns NULL if the property
 * is missing, empty, or not terminated within its value length.
 */
__EXTERN const char*
DeviceTreeReadString(
    _In_ const DeviceTreeNode_t* node,
    _In_ const char*             name);

/**
 * @brief Read a 32-bit or 64-bit big-endian integer property.
 *
 * Decodes one or two FDT cells into a host-endian 64-bit result. Other property
 * lengths are rejected.
 *
 * @param node Node containing the property.
 * @param name Exact property name to read.
 * @param valueOut Receives the decoded integer on success.
 * @return OS_EOK on success, OS_ENOENT if missing, or an error for an unsupported length.
 */
__EXTERN oserr_t
DeviceTreeReadInteger(
    _In_  const DeviceTreeNode_t* node,
    _In_  const char*             name,
    _Out_ uint64_t*               valueOut);

/**
 * @brief Check whether a node's compatible string list contains a value.
 *
 * Compares @p compatible against each NUL-terminated string in the node's
 * compatible property. Returns zero when the property is missing, malformed,
 * or has no exact match; otherwise returns nonzero.
 */
__EXTERN int
DeviceTreeIsCompatible(
    _In_ const DeviceTreeNode_t* node,
    _In_ const char*             compatible);

/**
 * @brief Check whether a node and all its ancestors are enabled.
 *
 * Nodes without a status property are treated as enabled. If a node has one,
 * its value must be the string "okay" or "ok"; any other or malformed value
 * disables that node and its descendants for this check.
 */
__EXTERN int
DeviceTreeIsEnabled(
    _In_ const DeviceTreeNode_t* node);

/**
 * @brief Find a node by absolute path or by a name in the /aliases node.
 *
 * Absolute paths are walked from the root. A non-absolute name is looked up as
 * a property in /aliases, whose string value supplies the target absolute
 * path. A colon and any text after it are ignored, allowing paths or aliases
 * with device options. Returns NULL if the path, alias, or target is absent.
 */
__EXTERN const DeviceTreeNode_t*
DeviceTreeFindPath(
    _In_ const DeviceTree_t* tree,
    _In_ const char*         path);

/**
 * @brief Find the node assigned a particular phandle.
 *
 * The phandle argument is a host-endian value. Zero and 0xffffffff are reserved
 * and never resolve to a node.
 * @return A borrowed node pointer, or NULL if the phandle is invalid or absent.
 */
__EXTERN const DeviceTreeNode_t*
DeviceTreeFindPhandle(
    _In_ const DeviceTree_t* tree,
    _In_ uint32_t            phandle);

/**
 * @brief Read one reg address/size tuple and translate it to the root bus.
 *
 * Uses the parent bus's #address-cells and #size-cells to select the zero-based
 * tuple from the node's reg property. The complete range must fit through each
 * ancestor bus's ranges mapping; a missing ranges property is not treated as an
 * identity mapping, while an empty ranges property is. This helper supports up
 * to two address and size cells; PCI three-cell addresses need a PCI-specific
 * binding driver and are not supported here.
 *
 * @param node Node whose reg property is read.
 * @param index Zero-based tuple index in reg.
 * @param addressOut Receives the translated root-bus address on success.
 * @param lengthOut Receives the tuple length in bytes on success.
 */
__EXTERN oserr_t
DeviceTreeReadRegister(
    _In_  const DeviceTreeNode_t* node,
    _In_  unsigned int            index,
    _Out_ uint64_t*               addressOut,
    _Out_ uint64_t*               lengthOut);

/**
 * @brief Resolve one provider-and-arguments entry in a phandle list property.
 *
 * Each entry consists of a provider phandle followed by the number of argument
 * cells specified by @p cellsName on that provider (for example, #clock-cells).
 * The selected provider is returned with its arguments decoded to host-endian
 * values. A maximum of 16 argument cells is supported.
 *
 * @param tree Tree used to resolve provider phandles.
 * @param node Node containing the list property.
 * @param propertyName Name of the property containing provider entries.
 * @param cellsName Provider property that specifies the number of argument cells.
 * @param index Zero-based entry index in the list.
 * @param referenceOut Receives the provider and its argument cells on success.
 */
__EXTERN oserr_t
DeviceTreeReadReference(
    _In_  const DeviceTree_t*     tree,
    _In_  const DeviceTreeNode_t* node,
    _In_  const char*             propertyName,
    _In_  const char*             cellsName,
    _In_  unsigned int            index,
    _Out_ DeviceTreeReference_t*  referenceOut);

/**
 * @brief Resolve one interrupt specifier and its interrupt-controller node.
 *
 * Prefers interrupts-extended when present. Otherwise reads the indexed entry
 * from interrupts and finds its controller from an interrupt-parent property
 * on the node or an ancestor, falling back to an ancestor that declares
 * #interrupt-cells. The result contains the controller and decoded specifier
 * cells; interrupt routing through interrupt-map is not performed here.
 *
 * @param tree Tree used to resolve interrupt-controller phandles.
 * @param node Node whose interrupt property is read.
 * @param index Zero-based interrupt entry index.
 * @param interruptOut Receives the controller and interrupt cells on success.
 */
__EXTERN oserr_t
DeviceTreeReadInterrupt(
    _In_  const DeviceTree_t*     tree,
    _In_  const DeviceTreeNode_t* node,
    _In_  unsigned int            index,
    _Out_ DeviceTreeReference_t*  interruptOut);

/**
 * @brief Get the kernel's published device tree, if initialization succeeded.
 *
 * Returns NULL until DeviceTreeParseFull successfully parses and initializes a
 * tree. The returned pointer is owned by the kernel and must not be destroyed
 * by callers.
 */
__EXTERN const DeviceTree_t*
DeviceTreeGet(void);

/**
 * @brief Parse a device tree, initialize platform discovery, and publish it.
 *
 * Creates a validated tree snapshot and, on supported architectures, performs
 * architecture-specific platform initialization using that tree. The tree is
 * published through DeviceTreeGet only after initialization succeeds; on
 * failure, the temporary tree is destroyed and the error is returned. This
 * operation allocates memory, so the memory subsystem must already be available.
 *
 * @param deviceTree Start of the firmware-provided FDT blob.
 * @param deviceTreeSize Number of readable bytes in that blob.
 * @return OS_EOK on successful initialization and publication, otherwise an error status.
 */
__EXTERN oserr_t
DeviceTreeParseFull(
    _In_ const void* deviceTree,
    _In_ uint32_t    deviceTreeSize);

#endif //!__DEVICETREE_H__
