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

#include <fdt/reader.h>
#include <devicetree.h>
#include <string.h>

static uint64_t
__ReadCells(
    _In_ const unsigned char* bytes,
    _In_ uint32_t             count)
{
    uint64_t value = 0;

    for (uint32_t i = 0; i < count; i++) {
        value = (value << 32) | FdtReadBe32(bytes + i * 4);
    }
    return value;
}

static oserr_t
__CellCount(
    _In_ const DeviceTreeNode_t* node,
    _In_ const char*             name,
    _In_ uint32_t                defaultValue,
    _In_ uint32_t*               countOut)
{
    const DeviceTreeProperty_t* property;

    property = DeviceTreeGetProperty(node, name);
    if (!property) {
        *countOut = defaultValue;
        return OS_EOK;
    }
    
    if (property->Length != 4) {
        return OS_EINVALPARAMS;
    }
    
    *countOut = FdtReadBe32(property->Value);
    return OS_EOK;
}

static oserr_t
__AddressCells(
    _In_  const DeviceTreeNode_t* node,
    _Out_ uint32_t*               addressCells,
    _Out_ uint32_t*               sizeCells)
{
    oserr_t status;

    // DTSpec defines defaults on each bus, not inheritance from its parent.
    status = __CellCount(node, "#address-cells", 2, addressCells);
    if (status != OS_EOK) {
        return status;
    }
    
    status = __CellCount(node, "#size-cells", 1, sizeCells);
    if (status != OS_EOK) {
        return status;
    }
    
    if (*addressCells > 2 || *sizeCells > 2) {
        return OS_ENOTSUPPORTED;
    }
    return OS_EOK;
}

static oserr_t
__TranslateAddress(
    _In_    const DeviceTreeNode_t* bus,
    _In_    uint64_t                length,
    _InOut_ uint64_t*               address)
{
    while (bus->Parent) {
        const DeviceTreeProperty_t* ranges = DeviceTreeGetProperty(bus, "ranges");

        uint32_t childCells;
        uint32_t parentCells;
        uint32_t sizeCells;
        uint32_t unused;
        uint32_t stride;
        int      matched = 0;
        oserr_t  status;

        if (!ranges) {
            return OS_ENOENT;
        }
        
        status = __AddressCells(bus, &childCells, &sizeCells);
        if (status != OS_EOK) {
            return status;
        }
        
        status = __AddressCells(bus->Parent, &parentCells, &unused);
        if (status != OS_EOK) {
            return status;
        }
        if (!ranges->Length) {
            bus = bus->Parent;
            continue;
        }
        
        stride = (childCells + parentCells + sizeCells) * 4;
        if (!stride || !sizeCells || ranges->Length % stride) {
            return OS_EINVALPARAMS;
        }
        
        for (uint32_t offset = 0; offset < ranges->Length; offset += stride) {
            const unsigned char* tuple = (const unsigned char*)ranges->Value + offset;

            uint64_t child = __ReadCells(tuple, childCells);
            uint64_t parent = __ReadCells(tuple + childCells * 4, parentCells);
            uint64_t extent = __ReadCells(tuple + (childCells + parentCells) * 4, sizeCells);
            uint64_t displacement;

            if (*address < child) {
                continue;
            }
            
            displacement = *address - child;
            if (displacement >= extent || length > extent - displacement) {
                continue;
            }
            
            if (displacement > UINT64_MAX - parent ||
                length > UINT64_MAX - parent - displacement) {
                return OS_EOVERFLOW;
            }
            
            *address = parent + displacement;
            matched = 1;
            break;
        }
        
        if (!matched) {
            return OS_ENOENT;
        }
        bus = bus->Parent;
    }
    return OS_EOK;
}

oserr_t
DeviceTreeReadRegister(
    _In_  const DeviceTreeNode_t* node,
    _In_  unsigned int            index,
    _Out_ uint64_t*               addressOut,
    _Out_ uint64_t*               lengthOut)
{
    const DeviceTreeProperty_t* property;
    const unsigned char*        tuple;
    uint32_t                    addressCells;
    uint32_t                    sizeCells;
    uint32_t                    stride;
    uint64_t                    address;
    uint64_t                    length;
    oserr_t                     status;

    property = DeviceTreeGetProperty(node, "reg");
    if (!property || !node->Parent) {
        return OS_ENOENT;
    }
    
    status = __AddressCells(node->Parent, &addressCells, &sizeCells);
    if (status != OS_EOK) {
        return status;
    }
    
    stride = (addressCells + sizeCells) * 4;
    if (!stride || property->Length % stride) {
        return OS_EINVALPARAMS;
    }
    
    if (index >= property->Length / stride) {
        return OS_ENOENT;
    }
    
    tuple = (const unsigned char*)property->Value + index * stride;
    address = __ReadCells(tuple, addressCells);
    length = __ReadCells(tuple + addressCells * 4, sizeCells);
    if (length > UINT64_MAX - address) {
        return OS_EOVERFLOW;
    }
    
    status = __TranslateAddress(node->Parent, length, &address);
    if (status != OS_EOK) {
        return status;
    }

    *addressOut = address;
    *lengthOut = length;
    return OS_EOK;
}

oserr_t
DeviceTreeReadReference(
    _In_  const DeviceTree_t*     tree,
    _In_  const DeviceTreeNode_t* node,
    _In_  const char*             propertyName,
    _In_  const char*             cellsName,
    _In_  unsigned int            index,
    _Out_ DeviceTreeReference_t*  referenceOut)
{
    const DeviceTreeProperty_t* property;
    uint32_t                    offset = 0;

    property = DeviceTreeGetProperty(node, propertyName);
    if (!property) {
        return OS_ENOENT;
    }
    if (property->Length & 3) {
        return OS_EINVALPARAMS;
    }
    
    while (offset < property->Length) {
        const unsigned char*    bytes = property->Value;
        const DeviceTreeNode_t* provider;
        uint32_t                cells;
        oserr_t                 status;

        provider = DeviceTreeFindPhandle(tree, FdtReadBe32(bytes + offset));
        offset += 4;
        if (!provider) {
            return OS_EINVALPARAMS;
        }
        
        status = __CellCount(provider, cellsName, UINT32_MAX, &cells);
        if (status != OS_EOK) {
            return status;
        }
        if (cells > 16) {
            return OS_ENOTSUPPORTED;
        }
        if (cells > (property->Length - offset) / 4) {
            return OS_EINVALPARAMS;
        }
        
        if (!index) {
            referenceOut->Provider = provider;
            referenceOut->CellCount = cells;
            for (uint32_t i = 0; i < cells; i++) {
                referenceOut->Cells[i] = FdtReadBe32(bytes + offset + i * 4);
            }
            return OS_EOK;
        }
        offset += cells * 4;
        index--;
    }
    return OS_ENOENT;
}

oserr_t
DeviceTreeReadInterrupt(
    _In_  const DeviceTree_t*     tree,
    _In_  const DeviceTreeNode_t* node,
    _In_  unsigned int            index,
    _Out_ DeviceTreeReference_t*  interruptOut)
{
    const DeviceTreeProperty_t* property;
    const DeviceTreeNode_t*     ancestor;
    const DeviceTreeNode_t*     provider = NULL;
    uint32_t                    cells;
    uint32_t                    stride;
    oserr_t                     status;

    property = DeviceTreeGetProperty(node, "interrupts-extended");
    if (property) {
        return DeviceTreeReadReference(
            tree,
            node,
            "interrupts-extended",
            "#interrupt-cells",
            index,
            interruptOut
        );
    }
    
    property = DeviceTreeGetProperty(node, "interrupts");
    if (!property) {
        return OS_ENOENT;
    }
    
    for (ancestor = node; ancestor; ancestor = ancestor->Parent) {
        const DeviceTreeProperty_t* parent = DeviceTreeGetProperty(ancestor, "interrupt-parent");

        if (parent) {
            if (parent->Length != 4) {
                return OS_EINVALPARAMS;
            }
            provider = DeviceTreeFindPhandle(tree, FdtReadBe32(parent->Value));
            break;
        }
        if (ancestor != node && DeviceTreeGetProperty(ancestor, "#interrupt-cells")) {
            provider = ancestor;
            break;
        }
    }
    
    if (!provider) {
        return OS_EINVALPARAMS;
    }
    
    status = __CellCount(provider, "#interrupt-cells", 0, &cells);
    if (status != OS_EOK) {
        return status;
    }
    if (!cells || cells > 16) {
        return OS_ENOTSUPPORTED;
    }
    
    stride = cells * 4;
    if (property->Length % stride) {
        return OS_EINVALPARAMS;
    }
    if (index >= property->Length / stride) {
        return OS_ENOENT;
    }
    
    interruptOut->Provider = provider;
    interruptOut->CellCount = cells;
    for (uint32_t i = 0; i < cells; i++) {
        interruptOut->Cells[i] = FdtReadBe32(
            (const unsigned char*)property->Value + index * stride + i * 4
        );
    }
    return OS_EOK;
}
