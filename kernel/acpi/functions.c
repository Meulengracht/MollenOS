/**
 * MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 *
 * ACPI(CA) Device Scan Interface
 */

#define __MODULE "ACPI"
//#define __TRACE

#define __need_minmax
#include <arch/utils.h>
#include <acpiinterface.h>
#include <assert.h>
#include <debug.h>
#include <heap.h>

static char AcpiGbl_DeviceInformationBuffer[1024];

ACPI_STATUS
AcpiVideoBacklightCapCallback(
    ACPI_HANDLE Handle,
    UINT32      Level,
    void*       Context,
    void**      ReturnValue)
{
    ACPI_HANDLE NullHandle = NULL;
    uint64_t *video_features = (uint64_t*)Context;

    if (ACPI_SUCCESS(AcpiGetHandle(Handle, "_BCM", &NullHandle)) &&
        ACPI_SUCCESS(AcpiGetHandle(Handle, "_BCL", &NullHandle))) {
        *video_features |= ACPI_VIDEO_BACKLIGHT;
        if (ACPI_SUCCESS(AcpiGetHandle(Handle, "_BQC", &NullHandle))) {
            *video_features |= ACPI_VIDEO_BRIGHTNESS;
        }
        return AE_CTRL_TERMINATE;
    }
    return AE_OK;
}

/* Is this a video device? 
 * If it is, we also retrieve capabilities */
ACPI_STATUS AcpiDeviceIsVideo(AcpiDevice_t *Device)
{
    ACPI_HANDLE NullHandle = NULL;
    uint64_t VidFeatures = 0;

    /* Sanity */
    if (Device == NULL)
        return AE_ABORT_METHOD;

    /* Does device support Video Switching */
    if (ACPI_SUCCESS(AcpiGetHandle(Device->Handle, "_DOD", &NullHandle)) &&
        ACPI_SUCCESS(AcpiGetHandle(Device->Handle, "_DOS", &NullHandle)))
        VidFeatures |= ACPI_VIDEO_SWITCHING;

    /* Does device support Video Rom? */
    if (ACPI_SUCCESS(AcpiGetHandle(Device->Handle, "_ROM", &NullHandle)))
        VidFeatures |= ACPI_VIDEO_ROM;

    /* Does device support configurable video head? */
    if (ACPI_SUCCESS(AcpiGetHandle(Device->Handle, "_VPO", &NullHandle)) &&
        ACPI_SUCCESS(AcpiGetHandle(Device->Handle, "_GPD", &NullHandle)) &&
        ACPI_SUCCESS(AcpiGetHandle(Device->Handle, "_SPD", &NullHandle)))
        VidFeatures |= ACPI_VIDEO_POSTING;

    /* Only call this if it is a video device */
    if (VidFeatures != 0)
    {
        AcpiWalkNamespace(ACPI_TYPE_DEVICE, Device->Handle, ACPI_UINT32_MAX,
            AcpiVideoBacklightCapCallback, NULL, &VidFeatures, NULL);

        /* Update ONLY if video device */
        Device->FeaturesEx |= VidFeatures;

        return AE_OK;
    }
    else 
        return AE_NOT_FOUND;
}

/* Is this a docking device? 
 * If it has a _DCK method, yes */
ACPI_STATUS AcpiDeviceIsDock(AcpiDevice_t *Device)
{
    ACPI_HANDLE NullHandle = NULL;

    /* Sanity */
    if (Device == NULL)
        return AE_ABORT_METHOD;
    else
        return AcpiGetHandle(Device->Handle, "_DCK", &NullHandle);
}

/* Is this a BAY (i.e cd-rom drive with a ejectable bay) 
 * We check several ACPI methods here */
ACPI_STATUS AcpiDeviceIsBay(AcpiDevice_t *Device)
{
    ACPI_STATUS Status;
    ACPI_HANDLE ParentHandle = NULL;
    ACPI_HANDLE NullHandle = NULL;

    /* Sanity, make sure it is even ejectable */
    Status = AcpiGetHandle(Device->Handle, "_EJ0", &NullHandle);

    if (ACPI_FAILURE(Status))
        return Status;

    /* Fine, lets try to fuck it up, _GTF, _GTM, _STM and _SDD,
     * we choose you! */
    if ((ACPI_SUCCESS(AcpiGetHandle(Device->Handle, "_GTF", &NullHandle))) ||
        (ACPI_SUCCESS(AcpiGetHandle(Device->Handle, "_GTM", &NullHandle))) ||
        (ACPI_SUCCESS(AcpiGetHandle(Device->Handle, "_STM", &NullHandle))) ||
        (ACPI_SUCCESS(AcpiGetHandle(Device->Handle, "_SDD", &NullHandle))))
        return AE_OK;

    /* Uh... ok... maybe we are sub-device of an ejectable parent */
    Status = AcpiGetParent(Device->Handle, &ParentHandle);

    if (ACPI_FAILURE(Status))
        return Status;

    /* Now, lets try to fuck up parent ! */
    if ((ACPI_SUCCESS(AcpiGetHandle(ParentHandle, "_GTF", &NullHandle))) ||
        (ACPI_SUCCESS(AcpiGetHandle(ParentHandle, "_GTM", &NullHandle))) ||
        (ACPI_SUCCESS(AcpiGetHandle(ParentHandle, "_STM", &NullHandle))) ||
        (ACPI_SUCCESS(AcpiGetHandle(ParentHandle, "_SDD", &NullHandle))))
        return AE_OK;

    return AE_NOT_FOUND;
}

/* Is this a video device?
* If it is, we also retrieve capabilities */
ACPI_STATUS AcpiDeviceIsBattery(AcpiDevice_t *Device)
{
    ACPI_HANDLE NullHandle = NULL;
    uint64_t BtFeatures = 0;

    /* Sanity */
    if (Device == NULL)
        return AE_ABORT_METHOD;

    /* Does device support extended battery infromation */
    if (ACPI_SUCCESS(AcpiGetHandle(Device->Handle, "_BIF", &NullHandle)))
    {
        if (ACPI_SUCCESS(AcpiGetHandle(Device->Handle, "_BIX", &NullHandle)))
            BtFeatures |= ACPI_BATTERY_EXTENDED;
        else
            BtFeatures |= ACPI_BATTERY_NORMAL;
    }
        

    /* Does device support us querying battery */
    if (ACPI_SUCCESS(AcpiGetHandle(Device->Handle, "_BST", &NullHandle)))
        BtFeatures |= ACPI_BATTERY_QUERY;

    /* Does device support querying of charge information */
    if (ACPI_SUCCESS(AcpiGetHandle(Device->Handle, "_BTM", &NullHandle)) &&
        ACPI_SUCCESS(AcpiGetHandle(Device->Handle, "_BCT", &NullHandle)))
        BtFeatures |= ACPI_BATTERY_CHARGEINFO;

    /* Does device support configurable capacity measurement */
    if (ACPI_SUCCESS(AcpiGetHandle(Device->Handle, "_BMA", &NullHandle)) &&
        ACPI_SUCCESS(AcpiGetHandle(Device->Handle, "_BMS", &NullHandle)))
        BtFeatures |= ACPI_BATTERY_CAPMEAS;

    /* Only call this if it is a video device */
    if (BtFeatures != 0)
    {
        /* Update ONLY if video device */
        Device->FeaturesEx |= BtFeatures;

        return AE_OK;
    }
    else
        return AE_NOT_FOUND;
}

/* Get Memory Configuration Range */
ACPI_STATUS AcpiDeviceGetMemConfigRange(AcpiDevice_t *Device)
{
    /* Unused */
    _CRT_UNUSED(Device);
    return (AE_OK);
}

/* Set Device Data Callback */
void AcpiDeviceSetDataCallback(ACPI_HANDLE Handle, void *Data)
{
    TODO("AcpiDeviceSetDataCallback is not implemented");
    _CRT_UNUSED(Handle);
    _CRT_UNUSED(Data);
}

/* AcpiDeviceAttachData
 * Stores custom context data for an individual acpi-device handle */
ACPI_STATUS
AcpiDeviceAttachData(
    _In_ AcpiDevice_t *Device,
    _In_ int Type)
{
    // Don't store data for fixed-types
    if ((Type != ACPI_BUS_TYPE_POWER) &&
        (Type != ACPI_BUS_TYPE_SLEEP)) {
        return AcpiAttachData(Device->Handle, AcpiDeviceSetDataCallback, (void*)Device);
    }
    return AE_OK;
}

ACPI_STATUS
AcpiDeviceQueryStatus(
    _In_ ACPI_HANDLE    acpiHandle,
    _Out_ unsigned int* deviceStatusOut)
{
    ACPI_STATUS status = AE_OK;
    ACPI_OBJECT acpiObject;
    ACPI_BUFFER acpiBuffer;

    acpiBuffer.Length  = sizeof(ACPI_OBJECT);
    acpiBuffer.Pointer = (char*)&acpiObject;

    status = AcpiEvaluateObjectTyped(acpiHandle, "_STA", NULL, &acpiBuffer, ACPI_TYPE_INTEGER);
    if (ACPI_SUCCESS(status)) {
        *deviceStatusOut = LODWORD(acpiObject.Integer.Value);
    }
    return status;
}

/* AcpiDeviceGetStatus
 * Retrieves the status of the device by querying the _STA method. */
ACPI_STATUS
AcpiDeviceGetStatus(
    _InOut_ AcpiDevice_t* Device)
{
    // Variables
    ACPI_STATUS Status     = AE_OK;
    unsigned int Flags         = 0;

    // Does the device support the method?
    if (Device->Features & ACPI_FEATURE_STA) {
        // Query the status of the device
        Status = AcpiDeviceQueryStatus(Device->Handle, &Flags);
        if (Status == AE_OK) {
            Device->Status = Flags;
        }
    }
    else {
        // The child in should not inherit the parents status if the parent is 
        // functioning but not present (ie does not support dynamic status)
        Device->Status = ACPI_STA_DEVICE_PRESENT | ACPI_STA_DEVICE_ENABLED |
            ACPI_STA_DEVICE_UI | ACPI_STA_DEVICE_FUNCTIONING;
    }
    
    return Status;
}

/* AcpiDeviceGetBusAndSegment
 * Retrieves the initial location on the bus for the device */
ACPI_STATUS
AcpiDeviceGetBusAndSegment(
    _InOut_ AcpiDevice_t* Device)
{
    // Variables
    ACPI_STATUS Status = AE_OK;
    
    // Buffers
    ACPI_BUFFER Buffer;
    ACPI_OBJECT Object;

    // Set initial
    Buffer.Length = sizeof(ACPI_OBJECT);
    Buffer.Pointer = (void*)&Object;
    
    if (Device->Features & ACPI_FEATURE_BBN) {
        Status = AcpiEvaluateObjectTyped(Device->Handle, 
            "_BBN", NULL, &Buffer, ACPI_TYPE_INTEGER);
        if (ACPI_SUCCESS(Status)) {
            Device->PciLocation.Bus = (UINT16)Object.Integer.Value;
        }
    }
    if (Device->Features & ACPI_FEATURE_SEG) {
        Status = AcpiEvaluateObjectTyped(Device->Handle, 
            "_SEG", NULL, &Buffer, ACPI_TYPE_INTEGER);
        if (ACPI_SUCCESS(Status)) {
            Device->PciLocation.Segment = (UINT16)Object.Integer.Value;
        }
    }

    // Done
    return Status;
}

/* Gets Device Name */
ACPI_STATUS AcpiDeviceGetBusId(AcpiDevice_t *Device, uint32_t Type)
{
    ACPI_STATUS Status = AE_OK;
    ACPI_BUFFER Buffer;
    char BusId[8];

    /* Memset bus_id */
    memset(BusId, 0, sizeof(BusId));

    /* Setup Buffer */
    Buffer.Pointer = BusId;
    Buffer.Length = sizeof(BusId);

    /* Get Object Name based on type */
    switch (Type)
    {
        case ACPI_BUS_SYSTEM:
            strcpy(Device->BusId, "ACPISB");
            break;
        case ACPI_BUS_TYPE_POWER:
            strcpy(Device->BusId, "POWERF");
            break;
        case ACPI_BUS_TYPE_SLEEP:
            strcpy(Device->BusId, "SLEEPF");
            break;
        default:
        {
            /* Get name */
            Status = AcpiGetName(Device->Handle, ACPI_SINGLE_NAME, &Buffer);

            /* Sanity */
            if (ACPI_SUCCESS(Status))
                strcpy(Device->BusId, BusId);
        } break;
    }

    return Status;
}

/* Gets Device Features */
ACPI_STATUS AcpiDeviceGetFeatures(AcpiDevice_t *Device)
{
    ACPI_STATUS Status;
    ACPI_HANDLE NullHandle = NULL;

    /* Supports dynamic status? */
    Status = AcpiGetHandle(Device->Handle, "_STA", &NullHandle);
    
    if (ACPI_SUCCESS(Status))
        Device->Features |= ACPI_FEATURE_STA;

    /* Is compatible ids present? */
    Status = AcpiGetHandle(Device->Handle, "_CID", &NullHandle);

    if (ACPI_SUCCESS(Status))
        Device->Features |= ACPI_FEATURE_CID;

    /* Supports removable? */
    Status = AcpiGetHandle(Device->Handle, "_RMV", &NullHandle);

    if (ACPI_SUCCESS(Status))
        Device->Features |= ACPI_FEATURE_RMV;

    /* Supports ejecting? */
    Status = AcpiGetHandle(Device->Handle, "_EJD", &NullHandle);

    if (ACPI_SUCCESS(Status))
        Device->Features |= ACPI_FEATURE_EJD;
    else
    {
        Status = AcpiGetHandle(Device->Handle, "_EJ0", &NullHandle);

        if (ACPI_SUCCESS(Status))
            Device->Features |= ACPI_FEATURE_EJD;
    }

    /* Supports device locking? */
    Status = AcpiGetHandle(Device->Handle, "_LCK", &NullHandle);

    if (ACPI_SUCCESS(Status))
        Device->Features |= ACPI_FEATURE_LCK;

    /* Supports power management? */
    Status = AcpiGetHandle(Device->Handle, "_PS0", &NullHandle);

    if (ACPI_SUCCESS(Status))
        Device->Features |= ACPI_FEATURE_PS0;
    else
    {
        Status = AcpiGetHandle(Device->Handle, "_PR0", &NullHandle);

        if (ACPI_SUCCESS(Status))
            Device->Features |= ACPI_FEATURE_PS0;
    }

    /* Supports wake? */
    Status = AcpiGetHandle(Device->Handle, "_PRW", &NullHandle);

    if (ACPI_SUCCESS(Status))
        Device->Features |= ACPI_FEATURE_PRW;
    
    /* Has IRQ Routing Table Present ?  */
    Status = AcpiGetHandle(Device->Handle, "_PRT", &NullHandle);

    if (ACPI_SUCCESS(Status))
        Device->Features |= ACPI_FEATURE_PRT;

    /* Has Current Resources Set ?  */
    Status = AcpiGetHandle(Device->Handle, "_CRS", &NullHandle);

    if (ACPI_SUCCESS(Status))
        Device->Features |= ACPI_FEATURE_CRS;

    /* Supports Bus Numbering ?  */
    Status = AcpiGetHandle(Device->Handle, "_BBN", &NullHandle);

    if (ACPI_SUCCESS(Status))
        Device->Features |= ACPI_FEATURE_BBN;

    /* Supports Bus Segment ?  */
    Status = AcpiGetHandle(Device->Handle, "_SEG", &NullHandle);

    if (ACPI_SUCCESS(Status))
        Device->Features |= ACPI_FEATURE_SEG;

    /* Supports PCI Config Space ?  */
    Status = AcpiGetHandle(Device->Handle, "_REG", &NullHandle);

    if (ACPI_SUCCESS(Status))
        Device->Features |= ACPI_FEATURE_REG;

    return AE_OK;
}

ACPI_STATUS
AcpiDeviceGetHWInfo(
    _InOut_ AcpiDevice_t *Device,
    _In_ ACPI_HANDLE ParentHandle,
    _In_ int Type)
{
    ACPI_PNP_DEVICE_ID_LIST *Cid = NULL;
    ACPI_DEVICE_INFO *DeviceInfo = NULL;
    ACPI_STATUS Status;
    const char *CidAdd = NULL;
    char *Hid = NULL;
    char *Uid = NULL;
    
    ACPI_BUFFER Buffer;

    // Zero out the static buffer and initialize buffer object
    memset(&AcpiGbl_DeviceInformationBuffer[0], 0, 
        sizeof(AcpiGbl_DeviceInformationBuffer));
    Buffer.Length = sizeof(AcpiGbl_DeviceInformationBuffer);
    Buffer.Pointer = &AcpiGbl_DeviceInformationBuffer[0];
    DeviceInfo = Buffer.Pointer;
    
    switch (Type) {
        case ACPI_BUS_TYPE_DEVICE: {
            // Generic device, gather information
            Status = AcpiGetObjectInfo(Device->Handle, &DeviceInfo);
            if (ACPI_FAILURE(Status)) {
                ERROR("AcpiGetObjectInfo() failed");
                return Status;
            }

            // Only store fields that are valid
            if (DeviceInfo->Valid & ACPI_VALID_HID) {
                Hid = DeviceInfo->HardwareId.String;
            }
            if (DeviceInfo->Valid & ACPI_VALID_UID) {
                Uid = DeviceInfo->UniqueId.String;
            }
            if (DeviceInfo->Valid & ACPI_VALID_CID) {
                Cid = &DeviceInfo->CompatibleIdList;
            }
            if (DeviceInfo->Valid & ACPI_VALID_ADR) {
                Device->Address = DeviceInfo->Address;
                Device->Features |= ACPI_FEATURE_ADR;
            }

            // Do special device-checks
            if (AcpiDeviceIsVideo(Device) == AE_OK) {
                CidAdd = "VIDEO";
            }
            else if (AcpiDeviceIsDock(Device) == AE_OK) {
                CidAdd = "DOCK";
            }
            else if (AcpiDeviceIsBay(Device) == AE_OK) {
                CidAdd = "BAY";
            }
            else if (AcpiDeviceIsBattery(Device) == AE_OK) {
                CidAdd = "BATT";
            }
        } break;

        // Fixed devices/features
        case ACPI_BUS_SYSTEM:
            Hid = "OSSYSBUS";
            break;
        case ACPI_BUS_TYPE_POWER:
            Hid = "MOSPWRBN";
            break;
        case ACPI_BUS_TYPE_PROCESSOR:
            Hid = "MOSCPU";
            break;
        case ACPI_BUS_TYPE_SLEEP:
            Hid = "MOSSLPBN";
            break;
        case ACPI_BUS_TYPE_THERMAL:
            Hid = "MOSTHERM";
            break;
        case ACPI_BUS_TYPE_PWM:
            Hid = "MOSPOWER";
            break;
    }

    // Fix for Root System Bus (\_SB)
    if (((ACPI_HANDLE)ParentHandle == ACPI_ROOT_OBJECT) 
        && (Type == ACPI_BUS_TYPE_DEVICE)) {
        Hid = "OSSYSTEM";
    }

    // Store identifiers for device
    if (Hid) {
        strcpy(Device->HId, Hid);
        Device->Features |= ACPI_FEATURE_HID;
    }
    if (Uid) {
        strcpy(Device->UId, Uid);
        Device->Features |= ACPI_FEATURE_UID;
    }
    
    // Finalize and store the CId's
    if (Cid != NULL || CidAdd != NULL) {
        ACPI_PNP_DEVICE_ID_LIST *List = NULL;
        ACPI_SIZE size = 0;
        UINT32 count = 0;

        // Handle existing cid information
        if (Cid) {
            size = Cid->ListSize;
        }
        else if (CidAdd) {
            size = sizeof(ACPI_PNP_DEVICE_ID_LIST);
            Cid = ACPI_ALLOCATE_ZEROED(size);
            Cid->ListSize = size;
            Cid->Count = 0;
        }

        /* Do we need to manually add extra entry ? */
        if (CidAdd) {
            size += sizeof(ACPI_PNP_DEVICE_ID_LIST);
        }

        // Allocate a copy of the cid list
        List = (ACPI_PNP_DEVICE_ID_LIST*)kmalloc((size_t)size);
        if (Cid) {
            memcpy(List, Cid, Cid->ListSize);
            count = Cid->Count;
        }
        if (CidAdd) {
            List->Ids[count].Length = sizeof(CidAdd) + 1;
            List->Ids[count].String = (char*)kmalloc(sizeof(CidAdd) + 1);
            strncpy(List->Ids[count].String, CidAdd, strlen(CidAdd));
            count++;
        }

        // Store information
        List->Count = count;
        List->ListSize = size;
        Device->CId = List;
        Device->Features |= ACPI_FEATURE_CID;
    }
    return AE_OK;
}

ACPI_STATUS
AcpiPackageUInt64(
    _In_ ACPI_OBJECT *Package,
    _In_ int Index,
    _Out_ UINT64 *Out)
{
    // Variables
    ACPI_OBJECT *Element;
    
    // Access and validate element
    Element = &Package->Package.Elements[Index];
    if (Element->Type != ACPI_TYPE_INTEGER) {
        return AE_BAD_PARAMETER;
    }

    // Update out and return
    *Out = Element->Integer.Value;
    return AE_OK;
}

ACPI_STATUS
AcpiPackageUInt32(
    _In_ ACPI_OBJECT *Package,
    _In_ int Index,
    _Out_ UINT32 *Out)
{
    // Variables
    ACPI_STATUS Status;
    UINT64 Temporary;

    // Parse 64 bit
    Status = AcpiPackageUInt64(Package, Index, &Temporary);
    if (ACPI_SUCCESS(Status)) {
        *Out = LODWORD(Temporary);
    }
    return Status;
}

ACPI_STATUS
AcpiPackageReference(
    _In_ ACPI_HANDLE Scope,
    _In_ ACPI_OBJECT *Object,
    _Out_ ACPI_HANDLE *Reference)
{
    // Variables
    ACPI_HANDLE Temporary = NULL;
    ACPI_STATUS Status;

    // Sanitize parameters
    if (Object == NULL) {
        return AE_BAD_PARAMETER;
    }

    // Handle reference type
    switch (Object->Type) {
        case ACPI_TYPE_LOCAL_REFERENCE:
        case ACPI_TYPE_ANY: {
            Temporary = Object->Reference.Handle;
        } break;
        case ACPI_TYPE_STRING: {
            Status = AcpiGetHandle(Scope, Object->String.Pointer, &Temporary);
            if (ACPI_FAILURE(Status)) {
                return Status;
            }
        } break;
        
        default: {
            return AE_BAD_PARAMETER;
        } break;
    }

    // Update out and return
    *Reference = Temporary;
    return AE_OK;
}

/* AcpiDeviceParsePower
 * Parses and validates the _PRW feature of a GPE device. */
ACPI_STATUS
AcpiDeviceParsePower(
    _InOut_ AcpiDevice_t *Device)
{
    // Variables
    AcpiDevicePower_t *Power = &Device->PowerSettings;
    ACPI_STATUS Status;
    ACPI_BUFFER Buffer;
    ACPI_OBJECT    *Object, *Object2;
    int i;

    // Setup the buffer object
    Buffer.Pointer = NULL;
    Buffer.Length = ACPI_ALLOCATE_BUFFER;

    // Run the _PRW
    Status = AcpiEvaluateObject(Device->Handle, "_PRW", NULL, &Buffer);
    if (ACPI_FAILURE(Status)) {
        return Status;
    }

    // Initiate pointer to inital resource
    Object = (ACPI_OBJECT*)Buffer.Pointer;
    if (Object == NULL) {
        return AE_NOT_FOUND;
    }
    if (!ACPI_PKG_VALID(Object, 2)) {
        goto Cleanup;
    }

    // Parse the lowest capable wake power state
    // It is contained in element 1 of the power package
    Status = AcpiPackageUInt32(Object, 1, &Power->LowestWakeState);
    if (ACPI_FAILURE(Status)) {
        goto Cleanup;
    }

    // Parse the first element (0) of the power package
    switch (Object->Package.Elements[0].Type) {
        case ACPI_TYPE_INTEGER: {
            // The value is the bit index in GPEx_EN
            Power->GpeBit = LODWORD(Object->Package.Elements[0].Integer.Value);
        } break;
        case ACPI_TYPE_PACKAGE: {
            // First element is the gpe handle, second element
            // is the bit index in GPEx_EN in the gpe block referenced
            // by the gpe handle
            Object2 = &Object->Package.Elements[0];
            if (!ACPI_PKG_VALID(Object2, 2)) {
                goto Cleanup;
            }

            // Extract gpe handle and bit
            Status = AcpiPackageReference(NULL, &Object2->Package.Elements[0],
                &Power->GpeHandle);
            if (ACPI_FAILURE(Status)) {
                goto Cleanup;
            }
            Status = AcpiPackageUInt32(Object2, 1, &Power->GpeBit);
            if (ACPI_FAILURE(Status)) {
                goto Cleanup;
            }
        } break;

        // Don't handle other types
        default: {
            goto Cleanup;
        }
    }

    // Parse element 2 to N
    Power->PowerResourceCount = MIN(APCI_MAX_PRW_RESOURCES, Object->Package.Count);
    for (i = 0; i < Power->PowerResourceCount; i++) {
        Power->PowerResources[i] = &Object->Package.Elements[i];
    }

Cleanup:
    // Cleanup
    if (Buffer.Pointer != NULL) {
        AcpiOsFree(Buffer.Pointer);
    }
    return Status;
}

// AcpiDeviceInitialize
// Derive correct IRQ
// if HID == ACPI0006 then AcpiInstallGpeBlock
// if ACPI_FEATURE_PRW then AcpiSetWakeGpe
// AcpiUpdateAllGpes must be called afterwards

// AcpiDeviceDestroy