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

#include <debug.h>
#include <heap.h>
#include <string.h>

#include "pci.h"

struct __PciWindow {
    struct PciAddressWindow Value;
    struct __PciWindow*     Next;
};

struct __PciRoot {
    struct AcpiPciRoot  Value;
    ACPI_HANDLE         Handle;
    struct __PciWindow* Windows;
    struct __PciRoot*   Next;
    int                 HasBusRange;
};

static struct __PciRoot* g_pciRoots;
static uint32_t          g_pciRootCount;
static oserr_t           g_pciRootStatus = OS_ENOENT;

static ACPI_STATUS
__Integer(
    _In_  ACPI_HANDLE handle,
    _In_  const char* name,
    _Out_ UINT64*     value)
{
    ACPI_OBJECT object;
    ACPI_BUFFER buffer = { sizeof(object), &object };
    ACPI_STATUS status;

    status = AcpiEvaluateObject(handle, (char*)name, NULL, &buffer);
    if (ACPI_FAILURE(status)) {
        return status;
    }

    if (object.Type != ACPI_TYPE_INTEGER) {
        return AE_TYPE;
    }

    *value = object.Integer.Value;
    return AE_OK;
}

static void
__FreeRoot(
    _In_ struct __PciRoot* root)
{
    struct __PciWindow* window;

    while (root->Windows != NULL) {
        window = root->Windows;
        root->Windows = window->Next;
        kfree(window);
    }
    kfree(root);
}

static ACPI_STATUS
__RootResource(
    _In_ ACPI_RESOURCE* resource,
    _In_ void*          context)
{
    struct __PciRoot*       root = context;
    ACPI_RESOURCE_ADDRESS64 address;
    struct PciAddressWindow value = { 0 };
    struct __PciWindow*     window;
    struct __PciWindow**    link;
    uint64_t                minimum;
    uint64_t                maximum;
    uint64_t                length;
    ACPI_STATUS             status;

    // Only address descriptors describe ranges that the host bridge forwards
    // to PCI. Other descriptors, and extended descriptors marked as consumers,
    // describe registers of the root bridge itself. Firmware has used the
    // Consumer bit of the older address descriptors inconsistently, so we
    // cannot use it to tell the two apart.
    if (resource->Type == ACPI_RESOURCE_TYPE_EXTENDED_ADDRESS64) {
        if (resource->Data.ExtAddress64.ProducerConsumer == ACPI_CONSUMER) {
            return AE_OK;
        }

        memset(&address, 0, sizeof(ACPI_RESOURCE_ADDRESS64));
        address.ResourceType = resource->Data.ExtAddress64.ResourceType;
        address.Decode = resource->Data.ExtAddress64.Decode;
        address.MinAddressFixed = resource->Data.ExtAddress64.MinAddressFixed;
        address.MaxAddressFixed = resource->Data.ExtAddress64.MaxAddressFixed;
        address.Info = resource->Data.ExtAddress64.Info;
        address.Address = resource->Data.ExtAddress64.Address;
    } else if (resource->Type == ACPI_RESOURCE_TYPE_ADDRESS16 ||
               resource->Type == ACPI_RESOURCE_TYPE_ADDRESS32 ||
               resource->Type == ACPI_RESOURCE_TYPE_ADDRESS64) {
        
        status = AcpiResourceToAddress64(resource, &address);
        if (ACPI_FAILURE(status)) {
            return status;
        }

        if (address.ResourceSource.StringLength) {
            return AE_SUPPORT;
        }
    } else {
        return AE_OK;
    }

    minimum = address.Address.Minimum;
    maximum = address.Address.Maximum;
    length = address.Address.AddressLength;
    if (!length) {
        return AE_OK;
    }

    // We only support windows that are already assigned. Subtractive or
    // relocatable ranges need a resource allocation policy, and deviced does
    // not have one yet.
    if (address.Decode != ACPI_POS_DECODE ||
        !address.MinAddressFixed || !address.MaxAddressFixed) {
        return AE_SUPPORT;
    }

    if (maximum < minimum || length - 1 != maximum - minimum) {
        return AE_BAD_DATA;
    }

    if (address.ResourceType == ACPI_BUS_NUMBER_RANGE) {
        if (root->HasBusRange || maximum > 255 || address.Address.TranslationOffset) {
            return AE_BAD_DATA;
        }
        root->Value.BusStart = (uint8_t)minimum;
        root->Value.BusEnd = (uint8_t)maximum;
        root->HasBusRange = 1;
        return AE_OK;
    }

    if (address.ResourceType == ACPI_IO_RANGE) {
        // Sparse I/O leaves holes between register addresses. A window can
        // only describe a contiguous range of bytes, so we cannot support it.
        if (address.Info.Io.TranslationType == ACPI_SPARSE_TRANSLATION) {
            return AE_SUPPORT;
        }
        value.Space = PciAddressIo;
    } else if (address.ResourceType == ACPI_MEMORY_RANGE) {
        value.Space = maximum > UINT32_MAX ? PciAddressMemory64 : PciAddressMemory32;
        if (address.Info.Mem.Caching == ACPI_PREFETCHABLE_MEMORY) {
            value.Attributes = PCI_WINDOW_PREFETCHABLE;
        }
    } else {
        return AE_SUPPORT;
    }

    if (address.Address.TranslationOffset > UINT64_MAX - minimum) {
        return AE_BAD_DATA;
    }
    
    value.BusBase = minimum;
    value.PhysicalBase = minimum + address.Address.TranslationOffset;
    value.Length = length;

    if (!__PciWindowValid(&value)) {
        return AE_BAD_DATA;
    }

    for (link = &root->Windows; *link != NULL; link = &(*link)->Next) {
        window = *link;
        if ((value.Space == PciAddressIo) != (window->Value.Space == PciAddressIo)) {
            continue;
        }
        if (minimum <= window->Value.BusBase + window->Value.Length - 1 &&
            window->Value.BusBase <= maximum) {
            return AE_BAD_DATA;
        }
        if (value.PhysicalBase <= window->Value.PhysicalBase + window->Value.Length - 1 &&
            window->Value.PhysicalBase <= value.PhysicalBase + length - 1) {
            return AE_BAD_DATA;
        }
    }

    window = kmalloc(sizeof(struct __PciWindow));
    if (!window) {
        return AE_NO_MEMORY;
    }

    window->Value = value;
    window->Next = NULL;

    *link = window;

    root->Value.WindowCount++;
    return AE_OK;
}

static ACPI_STATUS
__ScanRoot(
    _In_ ACPI_HANDLE handle,
    _In_ UINT32      level,
    _In_ void*       context,
    _In_ void**      result)
{
    ACPI_DEVICE_INFO*  info;
    struct __PciRoot*  root;
    struct __PciRoot** link;
    ACPI_STATUS        status;
    ACPI_BUFFER        resources = { ACPI_ALLOCATE_BUFFER, NULL };
    UINT64             value;

    (void)level;
    (void)context;
    (void)result;

    status = AcpiGetObjectInfo(handle, &info);
    if (ACPI_FAILURE(status)) {
        return status;
    }

    // ACPICA sets this flag when PNP0A03/PNP0A08 is in either _HID or _CID.
    value = info->Flags & ACPI_PCI_ROOT_BRIDGE;
    AcpiOsFree(info);
    if (!value) {
        return AE_OK;
    }

    status = __Integer(handle, "_STA", &value);
    if (status == AE_NOT_FOUND) {
        value = 0x0f;
    } else if (ACPI_FAILURE(status)) {
        return status;
    }

    if ((value & 3) != 3) {
        return AE_OK;
    }

    root = kmalloc(sizeof(struct __PciRoot));
    if (!root) {
        return AE_NO_MEMORY;
    }

    memset(root, 0, sizeof(struct __PciRoot));
    root->Handle = handle;
    value = 0;

    status = __Integer(handle, "_SEG", &value);
    if ((ACPI_FAILURE(status) && status != AE_NOT_FOUND) || value > UINT16_MAX) {
        goto invalid;
    }

    root->Value.Segment = (uint16_t)value;

    status = AcpiGetCurrentResources(handle, &resources);
    if (ACPI_FAILURE(status)) {
        goto invalid;
    }

    status = AcpiWalkResourceBuffer(&resources, __RootResource, root);
    AcpiOsFree(resources.Pointer);
    if (ACPI_FAILURE(status) || !root->HasBusRange) {
        goto invalid;
    }

    // The bus range comes from _CRS. If _BBN is present, it must match the
    // first bus of that range, as mixing two descriptions that disagree would
    // make us probe the wrong devices. Without _BBN, the bus start from _CRS
    // is used.
    status = __Integer(handle, "_BBN", &value);
    if (status != AE_NOT_FOUND && (ACPI_FAILURE(status) || value != root->Value.BusStart)) {
        goto invalid;
    }

    for (link = &g_pciRoots; *link != NULL; link = &(*link)->Next) {
        if ((*link)->Value.Segment == root->Value.Segment &&
            (*link)->Value.BusStart <= root->Value.BusEnd &&
            root->Value.BusStart <= (*link)->Value.BusEnd) {
            goto invalid;
        }
    }

    root->Value.Id = ++g_pciRootCount;
    *link = root;
    return AE_OK;

invalid:
    ERROR("ACPI PCI: root metadata rejected (status %u)", status);
    __FreeRoot(root);
    // Never publish a partial list, as a root we skipped could overlap one we
    // already accepted. AcpiPciScan frees all the roots when this fails.
    return AE_BAD_DATA;
}

void
AcpiPciScan(void)
{
    struct __PciRoot* root;
    ACPI_STATUS       status;

    status = AcpiGetDevices(NULL, __ScanRoot, NULL, NULL);
    if (ACPI_FAILURE(status)) {
        while (g_pciRoots) {
            root = g_pciRoots;
            g_pciRoots = root->Next;
            __FreeRoot(root);
        }
        g_pciRootCount = 0;
        g_pciRootStatus = status == AE_NO_MEMORY ? OS_EOOM : OS_EINVALPARAMS;
        return;
    }
    g_pciRootStatus = OS_EOK;
}

static oserr_t
__BridgeHandle(
    _In_  ACPI_HANDLE                  parent,
    _In_  const struct AcpiPciAddress* address,
    _Out_ ACPI_HANDLE*                 result)
{
    ACPI_HANDLE child = NULL;
    ACPI_HANDLE match = NULL;
    ACPI_STATUS status;
    UINT64      adr;

    for (;;) {
        status = AcpiGetNextObject(ACPI_TYPE_DEVICE, parent, child, &child);
        if (status == AE_NOT_FOUND) {
            break;
        }
        if (ACPI_FAILURE(status)) {
            // A namespace error does not mean the bridge is missing. Returning
            // OS_ENOENT here would let deviced swizzle past a firmware lookup
            // that failed.
            return OS_EUNKNOWN;
        }
        status = __Integer(child, "_ADR", &adr);
        if (status == AE_NOT_FOUND) {
            continue;
        }

        if (ACPI_FAILURE(status)) {
            return OS_EUNKNOWN;
        }

        if (adr == ((UINT64)address->Device << 16 | address->Function)) {
            if (match) {
                return OS_EINVALPARAMS;
            }
            match = child;
        }
    }

    if (!match) {
        return OS_ENOENT;
    }

    *result = match;
    return OS_EOK;
}

oserr_t
AcpiPciQuery(
    _In_  const struct AcpiPciQuery* query,
    _Out_ union AcpiPciResult*       result)
{
    struct __PciRoot*   root;
    struct __PciWindow* window;
    ACPI_HANDLE         bus;
    uint32_t            index;
    oserr_t             status;

    if (g_pciRootStatus != OS_EOK) {
        return g_pciRootStatus;
    }

    if (query->Type > AcpiPciQueryInterrupt || query->Reserved ||
        query->BridgeCount > ACPI_PCI_MAX_BRIDGES) {
        return OS_EINVALPARAMS;
    }

    index = 0;
    for (root = g_pciRoots; root != NULL; root = root->Next, index++) {
        if (query->Type == AcpiPciQueryRoot ? index == query->Index : root->Value.Id == query->RootId) {
            break;
        }
    }
    
    if (!root) {
        return OS_ENOENT;
    }

    if (query->Type == AcpiPciQueryRoot) {
        result->Root = root->Value;
        return OS_EOK;
    }

    if (query->Type == AcpiPciQueryWindow) {
        window = root->Windows;
        for (index = 0; window != NULL && index < query->Index; index++) {
            window = window->Next;
        }

        if (!window) {
            return OS_ENOENT;
        }

        result->Window = window->Value;
        return OS_EOK;
    }
    
    if (query->Address.Device > 31 || query->Address.Function > 7 ||
        query->Pin < 1 || query->Pin > 4) {
        return OS_EINVALPARAMS;
    }

    bus = root->Handle;
    for (index = 0; index < query->BridgeCount; index++) {
        if (query->Bridges[index].Device > 31 || query->Bridges[index].Function > 7) {
            return OS_EINVALPARAMS;
        }
        
        status = __BridgeHandle(bus, &query->Bridges[index], &bus);
        if (status != OS_EOK) {
            return status;
        }
    }
    return AcpiPciRoute(bus, query, &result->Interrupt);
}
