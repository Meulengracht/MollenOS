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
 * Firmware table providers (ACPI and DeviceTree)
 */

#define __MODULE "FIRM"

#include <devicetree.h>
#include <firmware.h>
#include <string.h>

#ifdef __OSCONFIG_ACPI_SUPPORT
#include <acpiinterface.h>
#endif

struct __FirmwareProvider {
    unsigned int Source;
    oserr_t     (*Query)(OSFirmwareInfo_t* infoOut);
    oserr_t     (*Locate)(const OSFirmwareTableKey_t* key, const void** dataOut, OSFirmwareTable_t* tableOut);
};

#ifdef __OSCONFIG_ACPI_SUPPORT
static oserr_t
__AcpiQuery(
    _In_ OSFirmwareInfo_t* infoOut)
{
    if (AcpiAvailable() == ACPI_NOT_AVAILABLE) {
        return OS_ENOENT;
    }
    
    infoOut->Acpi.Revision     = (uint8_t)((AcpiGbl_FADT.Header.Revision << 4) | (AcpiGbl_FADT.MinorRevision & 0xF));
    infoOut->Acpi.Century      = AcpiGbl_FADT.Century;
    infoOut->Acpi.IaBootFlags  = AcpiGbl_FADT.BootFlags;
    infoOut->Acpi.ArmBootFlags = AcpiGbl_FADT.ArmBootFlags;
    return OS_EOK;
}

static oserr_t
__AcpiLocate(
    _In_  const OSFirmwareTableKey_t* key,
    _Out_ const void**                dataOut,
    _Out_ OSFirmwareTable_t*          tableOut)
{
    ACPI_TABLE_HEADER* header;
    char               signature[sizeof(key->Signature) + 1];

    if (AcpiAvailable() == ACPI_NOT_AVAILABLE) {
        return OS_ENOENT;
    }

    // ACPICA instances are 1-based
    if (key->Instance >= UINT32_MAX) {
        return OS_ENOENT;
    }

    memcpy(&signature[0], &key->Signature[0], sizeof(key->Signature));
    signature[sizeof(key->Signature)] = '\0';
    if (ACPI_FAILURE(AcpiGetTable(&signature[0], key->Instance + 1, &header))) {
        return OS_ENOENT;
    }

    *dataOut          = header;
    tableOut->Length   = header->Length;
    tableOut->Revision = header->Revision;
    return OS_EOK;
}
#endif

static oserr_t
__DeviceTreeQuery(
    _In_ OSFirmwareInfo_t* infoOut)
{
    const DeviceTree_t* tree = DeviceTreeGet();
    if (tree == NULL) {
        return OS_ENOENT;
    }
    infoOut->DeviceTree.Size = tree->Size;
    return OS_EOK;
}

static oserr_t
__DeviceTreeLocate(
    _In_  const OSFirmwareTableKey_t* key,
    _Out_ const void**                dataOut,
    _Out_ OSFirmwareTable_t*          tableOut)
{
    const DeviceTree_t* tree = DeviceTreeGet();
    if (tree == NULL || key->Instance != 0) {
        return OS_ENOENT;
    }

    *dataOut           = tree->Blob;
    tableOut->Length   = tree->Size;
    tableOut->Revision = tree->Version;
    return OS_EOK;
}

// Ordered by preference, the first available provider is the primary one.
static const struct __FirmwareProvider g_providers[] = {
#ifdef __OSCONFIG_ACPI_SUPPORT
    { OSFIRMWARE_ACPI, __AcpiQuery, __AcpiLocate },
#endif
    { OSFIRMWARE_DEVICETREE, __DeviceTreeQuery, __DeviceTreeLocate },
};

#define PROVIDER_COUNT (sizeof(g_providers) / sizeof(g_providers[0]))

oserr_t
FirmwareQuery(
    _Out_ OSFirmwareInfo_t* infoOut)
{
    memset(infoOut, 0, sizeof(OSFirmwareInfo_t));
    for (size_t i = 0; i < PROVIDER_COUNT; i++) {
        if (g_providers[i].Query(infoOut) != OS_EOK) {
            continue;
        }

        infoOut->Available |= g_providers[i].Source;
        if (infoOut->Primary == OSFIRMWARE_NONE) {
            infoOut->Primary = g_providers[i].Source;
        }
    }
    return OS_EOK;
}

oserr_t
FirmwareLocate(
    _In_  const OSFirmwareTableKey_t* key,
    _Out_ const void**                dataOut,
    _Out_ OSFirmwareTable_t*          tableOut)
{
    for (size_t i = 0; i < PROVIDER_COUNT; i++) {
        if (g_providers[i].Source == key->Source) {
            return g_providers[i].Locate(key, dataOut, tableOut);
        }
    }
    return OS_ENOENT;
}
