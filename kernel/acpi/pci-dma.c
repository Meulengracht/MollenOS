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

#include <string.h>
#include "pci.h"

// Limits for reading the firmware namespace. A broken or hostile firmware can
// describe a huge or very deep tree, so we stop after this many objects (every
// object we look at counts, not only the ones we care about) or this many levels.
#define ACPI_PCI_DMA_MAX_OBJECTS 4096U
#define ACPI_PCI_DMA_MAX_DEPTH   256U

// Running state while reading the namespace for one PCI root.
struct __DmaScan {
    uint32_t               Visited;
    AcpiPciDmaProperties_t Properties;
    oserr_t                Status;
};

// Turns an ACPICA status into one of our own error codes.
static oserr_t
__NamespaceError(
    _In_ ACPI_STATUS status)
{
    return status == AE_NO_MEMORY ? OS_EOOM : OS_EUNKNOWN;
}

// Looks at one namespace object. If it is named _DMA we note that it exists. If
// it is named _CCA (and checkCoherency is set) we read its value, unless an
// earlier _CCA was already found. Every call counts against the object limit.
static oserr_t
__CheckPolicyObject(
    _In_    ACPI_HANDLE       object,
    _In_    bool              checkCoherency,
    _InOut_ struct __DmaScan* scan)
{
    char         name[ACPI_NAMESEG_SIZE + 1];
    ACPI_BUFFER  buffer = {sizeof(name), name};
    ACPI_OBJECT  value;
    ACPI_STATUS  status;

    if (scan->Visited == ACPI_PCI_DMA_MAX_OBJECTS) {
        return OS_ENOTSUPPORTED;
    }
    
    scan->Visited++;
    
    status = AcpiGetName(object, ACPI_SINGLE_NAME, &buffer);
    if (ACPI_FAILURE(status)) {
        return __NamespaceError(status);
    }
    
    if (!memcmp(name, "_DMA", ACPI_NAMESEG_SIZE)) {
        scan->Properties.HasDma = true;
        return OS_EOK;
    }
    
    if (!checkCoherency || scan->Properties.CoherencyPresent || memcmp(name, "_CCA", ACPI_NAMESEG_SIZE)) {
        return OS_EOK;
    }

    buffer.Length = sizeof(value);
    buffer.Pointer = &value;
    
    status = AcpiEvaluateObject(object, NULL, NULL, &buffer);
    if (status == AE_BUFFER_OVERFLOW) {
        return OS_EINVALPARAMS;
    } else if (ACPI_FAILURE(status)) {
        return __NamespaceError(status);
    }

    if (value.Type != ACPI_TYPE_INTEGER || value.Integer.Value > 1) {
        return OS_EINVALPARAMS;
    }

    scan->Properties.CoherencyPresent = true;
    scan->Properties.Coherent = value.Integer.Value != 0;
    return OS_EOK;
}

// Checks the direct children of parent, without going into their own children.
static oserr_t
__CheckParentPolicy(
    _In_    ACPI_HANDLE       parent,
    _In_    bool              checkCoherency,
    _InOut_ struct __DmaScan* scan)
{
    ACPI_HANDLE child = NULL;
    ACPI_STATUS acpiStatus;
    oserr_t     status;

    for (;;) {
        acpiStatus = AcpiGetNextObject(ACPI_TYPE_ANY, parent, child, &child);
        if (acpiStatus == AE_NOT_FOUND) {
            return OS_EOK;
        } else if (ACPI_FAILURE(acpiStatus)) {
            return __NamespaceError(acpiStatus);
        }

        status = __CheckPolicyObject(child, checkCoherency, scan);
        if (status != OS_EOK) {
            return status;
        }
    }
}

/**
 * Checks the objects on the way down to the root, starting at \_SB (the top of
 * the device tree) and ending with the root's parent. The order matters because
 * the first _CCA found wins.
 */
static oserr_t
__ReadPathPolicy(
    _In_    ACPI_HANDLE       root,
    _InOut_ struct __DmaScan* scan)
{
    ACPI_HANDLE      path[ACPI_PCI_DMA_MAX_DEPTH];
    ACPI_HANDLE      systemBus;
    ACPI_HANDLE      current = root;
    ACPI_OBJECT_TYPE type;
    uint32_t         count = 0;
    ACPI_STATUS      acpiStatus;
    oserr_t          status;

    acpiStatus = AcpiGetHandle(NULL, "\\_SB", &systemBus);
    if (acpiStatus == AE_NOT_FOUND) {
        return OS_ENOTSUPPORTED;
    } else if (ACPI_FAILURE(acpiStatus)) {
        return __NamespaceError(acpiStatus);
    }

    while (current != systemBus) {
        if (count == SIZEOF_ARRAY(path)) {
            return OS_ENOTSUPPORTED;
        }

        path[count++] = current;

        acpiStatus = AcpiGetParent(current, &current);
        if (ACPI_FAILURE(acpiStatus)) {
            return __NamespaceError(acpiStatus);
        }
    }

    if (count == 0) {
        return OS_EINVALPARAMS;
    }

    // \_SB is only a container, so a _CCA directly inside it is not looked at.
    // We still note a _DMA there, before moving on to the devices below it.
    status = __CheckParentPolicy(systemBus, false, scan);
    if (status != OS_EOK) {
        return status;
    }

    for (uint32_t i = count; i > 1; i--) {
        acpiStatus = AcpiGetType(path[i - 1], &type);
        if (ACPI_FAILURE(acpiStatus)) {
            return __NamespaceError(acpiStatus);
        }

        status = __CheckParentPolicy(path[i - 1], type == ACPI_TYPE_DEVICE, scan);
        if (status != OS_EOK) {
            return status;
        }
    }
    return OS_EOK;
}

// Called by ACPICA for each object below the root. Level 1 is the root's
// direct children.
static ACPI_STATUS
__CheckSubtreePolicy(
    _In_ ACPI_HANDLE object,
    _In_ UINT32      level,
    _In_ void*       context,
    _In_ void**      result)
{
    struct __DmaScan* scan = context;
    (void)result;

    if (level > ACPI_PCI_DMA_MAX_DEPTH) {
        scan->Status = OS_ENOTSUPPORTED;
        return AE_CTRL_TERMINATE;
    }

    // Only the root's direct children can give it a _CCA. If a parent above the
    // root already gave one, it applies to everything below and stays as is.
    scan->Status = __CheckPolicyObject(object, level == 1, scan);
    if (scan->Status != OS_EOK) {
        return AE_CTRL_TERMINATE;
    }
    return AE_OK;
}

oserr_t
AcpiPciReadDmaProperties(
    _In_  ACPI_HANDLE             rootHandle,
    _Out_ AcpiPciDmaProperties_t* propertiesOut)
{
    struct __DmaScan scan = {0};
    ACPI_STATUS      acpiStatus;
    oserr_t          status;

    if (rootHandle == NULL || propertiesOut == NULL) {
        return OS_EINVALPARAMS;
    }

    status = __ReadPathPolicy(rootHandle, &scan);
    if (status != OS_EOK) {
        return status;
    }

    // Walk one level deeper than we accept. That way a tree that is too deep
    // gets reported, instead of us quietly skipping objects below the limit.
    acpiStatus = AcpiWalkNamespace(
        ACPI_TYPE_ANY,
        rootHandle,
        ACPI_PCI_DMA_MAX_DEPTH + 1,
        __CheckSubtreePolicy,
        NULL,
        &scan,
        NULL
    );
    if (ACPI_FAILURE(acpiStatus)) {
        return __NamespaceError(acpiStatus);
    }

    if (scan.Status != OS_EOK) {
        return scan.Status;
    }
    
    *propertiesOut = scan.Properties;
    return OS_EOK;
}
