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
 * Built from docs/specifications/devicetree-specification-v0.4.pdf
 */

#ifndef __FDT_READER_H__
#define __FDT_READER_H__

#include <os/osdefs.h>

/**
 * @brief Flattened Device Tree (FDT) parser.
 * All fields are stored in big-endian format, so before values are decoded
 * we must convert them to the host's endianness before use.
 */

struct FDTHeader {
    // Contains 0xD00DFEED (big-endian)
    uint32_t Magic;
    // This field shall contain the total size in bytes of the 
    // devicetree data structure
    uint32_t TotalSize;
    // Offset to the structure block
    uint32_t OffDtStruct;
    // Offset to the strings block
    uint32_t OffDtStrings;
    // Offset to the memory reservation block
    uint32_t OffMemRsvmap;
    // Version of the device tree format, we support version 17
    uint32_t Version;
    // Last compatible version of the device tree format
    uint32_t LastCompVersion;
    // Physical ID of the boot CPU
    uint32_t BootCpuidPhys;
    // Size of the strings block
    uint32_t SizeDtStrings;
    // Size of the structure block
    uint32_t SizeDtStruct;
};

/**
 * @brief The memory reservation block provides the client program with a list of areas in physical memory which are reserved; that
 * is, which shall not be used for general memory allocations. It is used to protect vital data structures from being overwritten
 * by the client program. For example, on some systems with an IOMMU, the TCE (translation control entry) tables initialized
 * by a DTSpec boot program would need to be protected in this manner. Likewise, any boot program code or data used
 * during the client program’s runtime would need to be reserved (e.g., RTAS on Open Firmware platforms). DTSpec does
 * not require the boot program to provide any such runtime components, but it does not prohibit implementations from doing
 * so as an extension.
 * 
 * As with the /reserved-memory node (Section 3.5.4), when booting via [UEFI] entries in the Memory Reservation Block
 * must also be listed in the system memory map obtained via the GetMemoryMap() to protect against allocations by UEFI
 * applications. The memory reservation block entries should be listed with type EfiReservedMemoryType.
 */

struct FDTReserveEntry {
    uint64_t Address; // Starting physical address of the reserved range (big-endian in the blob).
    uint64_t Size;    // Reserved range length in bytes (big-endian in the blob).
};

/**
 * @brief The structure block is composed of a sequence of pieces, each beginning with a token, that is, a big-endian 32-bit integer.
 * Some tokens are followed by extra data, the format of which is determined by the token value. All tokens shall be aligned
 * on a 32-bit boundary, which may require padding bytes (with a value of 0x0) to be inserted after the previous token’s data.
 * 
 * The devicetree structure is represented as a linear tree: the representation of each node begins with an FDT_BEGIN_NODE
 * token and ends with an FDT_END_NODE token. The node’s properties and subnodes (if any) are represented before the
 * FDT_END_NODE, so that the FDT_BEGIN_NODE and FDT_END_NODE tokens for those subnodes are nested within
 * those of the parent.
 * The structure block as a whole consists of the root node’s representation (which contains the representations for all other
 * nodes), followed by an FDT_END token to mark the end of the structure block as a whole.
 */

enum FDTToken {
    FDT_BEGIN_NODE = 0x1,
    FDT_END_NODE = 0x2,
    FDT_PROP = 0x3,
    FDT_NOP = 0x4,
    FDT_END = 0x9,
};

struct FDTProperty {
    uint32_t Length;     // Number of property-value bytes, in big-endian form in the blob.
    uint32_t NameOffset; // Byte offset of the property's null-terminated name within the strings block.
    uint8_t  Value[];    // Property value bytes; the structure block pads the following data to 4-byte alignment.
};

// Maximum number of nested nodes accepted by FdtParseStructure, including the root.
#define FDT_MAX_DEPTH 32

/**
 * @brief Callbacks used to receive a parsed FDT tree as traversal events.
 *
 * The parser calls BeginNode when it enters a node, Property for each property,
 * and EndNode when it leaves a node. Names and property values point into the
 * input blob and are borrowed, read-only data; callbacks must not free or modify
 * them, and the caller must keep the blob alive while using them. The parser
 * does not allocate memory or roll back callback effects. A later parse error
 * can occur after earlier callbacks have run, so consumers should publish
 * accumulated results only after the parse function returns success.
 */
struct FdtParser {
    oserr_t (*BeginNode)(void* userData, const char* name, uint32_t nameLength); // Called with a node name and its byte length (excluding NUL).
    oserr_t (*Property)(void* userData, const char* name, const void* value, uint32_t valueLength); // Called with a property name and its raw value bytes.
    oserr_t (*EndNode)(void* userData); // Called when the current node closes.
    void*    UserData; // Caller-owned context passed unchanged to every callback.
};

/**
 * @brief Decode one 32-bit big-endian value without requiring aligned input.
 *
 * Reads exactly four bytes from @p value and returns the corresponding
 * host-endian integer. The pointer must refer to at least four readable bytes.
 */
static inline uint32_t
FdtReadBe32(
    _In_ const void* value)
{
    const uint8_t* p = value;
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

/**
 * @brief Decode and validate the fixed-size header at the start of an FDT blob.
 *
 * Reads the ten 32-bit header fields, converts them from big-endian to host
 * endian, and checks the FDT signature, supported format compatibility, total
 * size, block ordering, alignment, and block bounds. On success, @p header
 * contains decoded values and offsets measured from the start of @p deviceTree.
 * The reservation-map entries themselves are not parsed by this function.
 *
 * @param deviceTree Start of the complete FDT blob.
 * @param size Number of readable bytes available at @p deviceTree.
 * @param header Output structure receiving the decoded header fields.
 * @return OS_EOK if the header and its block layout are supported and in bounds;
 *         otherwise an error status.
 */
__EXTERN oserr_t
FdtParseHeader(
    _In_  const void*       deviceTree,
    _In_  uint32_t          size,
    _Out_ struct FDTHeader* header);

/**
 * @brief Validate an FDT structure block and report its contents through callbacks.
 *
 * Walks the node/property token stream, resolves property names through the
 * strings block, checks node nesting and block bounds, and invokes the matching
 * callbacks in document order. The root node must be present and the final
 * FDT_END token must end the supplied structure block. Callback errors stop the
 * walk and are returned to the caller; malformed input returns an error too.
 * Callbacks may already have run when an error is returned, so their effects
 * are not automatically undone.
 *
 * @param structureBlock Structure-block bytes beginning at the first token.
 * @param structureBlockSize Length of that block in bytes; must be 4-byte aligned.
 * @param stringBlock Strings block used to resolve property-name offsets.
 * @param stringBlockSize Length of the strings block in bytes.
 * @param context Non-null set of traversal callbacks and caller-owned callback data.
 * @return OS_EOK on a complete valid tree, otherwise an error status.
 */
__EXTERN oserr_t
FdtParseStructure(
    _In_ const void*             structureBlock,
    _In_ uint32_t                structureBlockSize,
    _In_ const char*             stringBlock,
    _In_ uint32_t                stringBlockSize,
    _In_ struct FdtParser* context);

/**
 * @brief Parse and report the property records belonging to one node.
 *
 * @p properties must point to the start of the node's property span: immediately
 * after its padded name. @p length must end at the end of the last property
 * record, before any child-node tokens. The span may contain FDT_NOP tokens;
 * every other token must be FDT_PROP. Property names are looked up in the
 * strings block and each value is passed to the parser's Property callback.
 * The property name and value pointers borrow memory from the input blocks,
 * which must remain alive and unchanged while callbacks use them. If parsing
 * fails, callbacks for earlier properties may already have run.
 *
 * @param properties Start of the node's property-token span.
 * @param length Length of that span in bytes.
 * @param strings Strings block containing property names.
 * @param stringsLength Length of the strings block in bytes.
 * @param parser Parser context with a valid Property callback.
 * @return OS_EOK if the entire span contains valid properties, otherwise an error status.
 */
oserr_t
FdtVisitProperties(
    _In_ const void* properties,
    _In_ uint32_t length,
    _In_ const char* strings,
    _In_ uint32_t stringsLength,
    _In_ struct FdtParser* parser);

#endif //!__FDT_READER_H__
