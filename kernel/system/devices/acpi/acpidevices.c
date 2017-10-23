/* MollenOS
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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * MollenOS MCore - ACPI(CA) Device Scan Interface
 */

#define __MODULE "DSIF"
//#define __TRACE

/* Includes 
 * - System */
#include <system/utils.h>
#include <acpiinterface.h>
#include <interrupts.h>
#include <debug.h>
#include <heap.h>

/* Includes
 * - Library */
#include <stdio.h>
#include <assert.h>

/* Internal Use */
typedef struct _IrqResource {
    int                      Gathering;
	List_t					*IrqList;
	PciRoutingEntry_t       *IrqActive;
} IrqResource_t;

/* Globals
 * - State keeping variables and static buffers */
static char AcpiGbl_DeviceInformationBuffer[1024];

/* Video Backlight Capability Callback */
ACPI_STATUS
AcpiVideoBacklightCapCallback(
    ACPI_HANDLE Handle,
    UINT32 Level,
    void *Context,
    void **ReturnValue)
{
	ACPI_HANDLE NullHandle = NULL;
	uint64_t *video_features = (uint64_t*)Context;

	if (ACPI_SUCCESS(AcpiGetHandle(Handle, "_BCM", &NullHandle)) &&
		ACPI_SUCCESS(AcpiGetHandle(Handle, "_BCL", &NullHandle)))
	{
		/* Add */
		*video_features |= ACPI_VIDEO_BACKLIGHT;
		
		/* Check for Brightness */
		if (ACPI_SUCCESS(AcpiGetHandle(Handle, "_BQC", &NullHandle)))
			*video_features |= ACPI_VIDEO_BRIGHTNESS;
		
		/* We have backlight support, no need to scan further */
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
	// @todo
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

/* AcpiDeviceQueryStatus
 * Internal function for querying the status of a ACPI_HANDLE */
ACPI_STATUS
AcpiDeviceQueryStatus(
	_In_ ACPI_HANDLE Handle, 
	_Out_ Flags_t *DeviceStatus)
{
	// Variables
	ACPI_STATUS Status = AE_OK;

	// Buffers
	ACPI_OBJECT Object;
	ACPI_BUFFER Buffer;

	// Initialize buffer
	Buffer.Length = sizeof(ACPI_OBJECT);
	Buffer.Pointer = (char*)&Object;
	Status = AcpiEvaluateObjectTyped(Handle, "_STA", NULL, &Buffer, ACPI_TYPE_INTEGER);
	if (ACPI_SUCCESS(Status)) {
		*DeviceStatus = Object.Integer.Value;
	}
	else {
		*DeviceStatus = 0;
	}

	// Done
	return Status;
}

/* AcpiDeviceGetStatus
 * Retrieves the status of the device by querying the _STA method. */
ACPI_STATUS
AcpiDeviceGetStatus(
	_InOut_ AcpiDevice_t* Device)
{
	// Variables
	ACPI_STATUS Status 	= AE_OK;
	Flags_t Flags 		= 0;

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

/* AcpiDeviceIrqRoutingCallback
 * Resource Callback for _CSR or _PRS methods */
ACPI_STATUS
AcpiDeviceIrqRoutingCallback(
    ACPI_RESOURCE *Resource, 
    void *Context)
{
    // Variables
    IrqResource_t *IrqResource       = NULL;
    DataKey_t pKey;

    // Debug
    TRACE("AcpiDeviceIrqRoutingCallback(Type %u)", Resource->Type);

    // Sanitize the type of resource
    if (Resource->Type == ACPI_RESOURCE_TYPE_END_TAG) {
        return AE_OK;
    }

	// Initiate values
    IrqResource = (IrqResource_t*)Context;
	pKey.Value = 0;

	// Right now we are just looking for Irq's
	if (Resource->Type == ACPI_RESOURCE_TYPE_IRQ) {
		ACPI_RESOURCE_IRQ *Irq  = NULL;
		UINT8 i;
        
        // Initialize values
        Irq = &Resource->Data.Irq;
        
        // Sanitize IRQ entry
        if (Irq == NULL || Irq->InterruptCount == 0) {
            if (IrqResource->Gathering == 0) {
                WARNING("Blank _CSR IRQ resource entry. Device is disabled.");
            }
            else {
                WARNING("Blank _PRS IRQ resource entry.");
            }
            return AE_OK;
        }

		// Iterate all possible interrupts (InterruptCount)
		for (i = 0; i < Irq->InterruptCount; i++) {
			// Set initial members
			PciRoutingEntry_t *RoutingEntry = 
				(PciRoutingEntry_t*)kmalloc(sizeof(PciRoutingEntry_t));
			RoutingEntry->AcType = ACPI_RESOURCE_TYPE_IRQ;
			RoutingEntry->Polarity = Irq->Polarity;
			RoutingEntry->Trigger = Irq->Triggering;
			RoutingEntry->Shareable = Irq->Sharable;
			RoutingEntry->Irq = Irq->Interrupts[i];
			TRACE("Irq %u found, count: %u", RoutingEntry->Irq, Irq->InterruptCount);

			// Are we just finding the active irq?
			if (IrqResource->Gathering == 0) {
				if (IrqResource->IrqActive == NULL) {
					IrqResource->IrqActive = RoutingEntry;
				}
				else {
					kfree(RoutingEntry);
				}
				break;
			}
			else {
				// Append to list of irqs
				ListAppend(IrqResource->IrqList, ListCreateNode(pKey, pKey, RoutingEntry));
			}
		}
	}
	else if (Resource->Type == ACPI_RESOURCE_TYPE_EXTENDED_IRQ) {
		ACPI_RESOURCE_EXTENDED_IRQ *Irq  = NULL;
		UINT8 i;
 
        // Initialize values
        Irq = &Resource->Data.ExtendedIrq;

        // Sanitize IRQ entry
        if (Irq == NULL || Irq->InterruptCount == 0) {
            if (IrqResource->Gathering == 0) {
                WARNING("Blank _CSR IRQ resource entry. Device is disabled.");
            }
            else {
                WARNING("Blank _PRS IRQ resource entry.");
            }
            return AE_OK;
		}
		
		// Iterate all possible interrupts (InterruptCount)
		for (i = 0; i < Irq->InterruptCount; i++) {
			// Set initial members
			PciRoutingEntry_t *RoutingEntry = 
				(PciRoutingEntry_t*)kmalloc(sizeof(PciRoutingEntry_t));
			RoutingEntry->AcType = ACPI_RESOURCE_TYPE_EXTENDED_IRQ;
			RoutingEntry->Polarity = Irq->Polarity;
			RoutingEntry->Trigger = Irq->Triggering;
			RoutingEntry->Shareable = Irq->Sharable;
			RoutingEntry->Irq = Irq->Interrupts[i];
			TRACE("Irq %u found, count: %u", RoutingEntry->Irq, Irq->InterruptCount);

			// Are we just finding the active irq?
			if (IrqResource->Gathering == 0) {
				if (IrqResource->IrqActive == NULL) {
					IrqResource->IrqActive = RoutingEntry;
				}
				else {
					kfree(RoutingEntry);
				}
				break;
			}
			else {
				// Append to list of irqs
				ListAppend(IrqResource->IrqList, ListCreateNode(pKey, pKey, RoutingEntry));
			}
		}
	}

	return AE_OK;
}

/* AcpiGetLeastLoaded 
 * Retrieves the least loaded irq-line from the possible irqs */
PciRoutingEntry_t*
AcpiGetLeastLoaded(
	_In_ List_t *RoutingEntries)
{
	// Variables
	int InterruptList[64];
    int Count = 0;
    
    // Debug
    TRACE("AcpiGetLeastLoaded()");

	// Sum up and transfer to int array
	foreach(iNode, RoutingEntries) {
		PciRoutingEntry_t *Entry = (PciRoutingEntry_t*)iNode->Data;
		InterruptList[Count] = Entry->Irq;
		Count++;
	}

	// Get least loaded
	Count = InterruptGetLeastLoaded(InterruptList, Count);
	if (Count == INTERRUPT_NONE) {
        ERROR("No valid interrupt found");
		return NULL;
	}

	// Lookup selected
	_foreach(iNode, RoutingEntries) {
		PciRoutingEntry_t *Entry = (PciRoutingEntry_t*)iNode->Data;
		if (Entry->Irq == Count) {
			return Entry;
		}
	}

    // The hell??
    ERROR("Couldn't refind interrupt %i", Count);
	return NULL;
}

/* AcpiDeviceSelectIrq
 * It validates the current active irq and matches it against
 * the possible irqs of the device. Selects the best possible irq */
ACPI_STATUS
AcpiDeviceSelectIrq(
    _InOut_ PciRoutingSource_t *Source)
{
    // Variables
    PciRoutingEntry_t *SelectedEntry = NULL;
	Flags_t DeviceStatus			 = 0;
	ACPI_STATUS Status				 = AE_OK;

    // Buffers
    ACPI_BUFFER Buffer;
    struct {
		ACPI_RESOURCE Irq;
		ACPI_RESOURCE End;
	} *Resource;

    // Check that we have an active irq and that it
    // exists in the possible irq-list
    if (Source->ActiveEntry != NULL) {
        TRACE("Irq %u is active, validating", Source->ActiveEntry->Irq);
        foreach(iNode, Source->Entries) {
			PciRoutingEntry_t *Entry = (PciRoutingEntry_t*)iNode->Data;
			if (Entry->Irq == Source->ActiveEntry->Irq) {
				if (Entry != Source->ActiveEntry) {
					kfree(Source->ActiveEntry);
					Source->ActiveEntry = Entry;
                }
                
                // Make sure interrupt-manager knows about our allocation
                if (InterruptIncreasePenalty(SelectedEntry->Irq) != OsSuccess) {
                    ERROR("Failed to increase penalty for irq");
                }
                
				return AE_OK;
			}
		}
		
		// Reasons we end up here:
		// 1 We currently had an active irq not possible, choose a new
    }
	
	// Get the best possible irq currently for load-balancing
	SelectedEntry = AcpiGetLeastLoaded(Source->Entries);

    // Sanitize
    if (SelectedEntry == NULL) {
        TRACE("No possible irq for device out of %u entries", 
            ListLength(Source->Entries));
		return AE_ERROR;
    }

    // Debug
    TRACE("Updating device with irq %u", SelectedEntry->Irq);

    // Initiate objects
    Resource = kmalloc(sizeof(*Resource) + 1);
    memset(Resource, 0, sizeof(sizeof(*Resource) + 1));
    Buffer.Length = sizeof(*Resource) + 1;
    Buffer.Pointer = (void*)Resource;

    // Fill out
    if (SelectedEntry->AcType == ACPI_RESOURCE_TYPE_IRQ) {
        Resource->Irq.Type = ACPI_RESOURCE_TYPE_IRQ;
        Resource->Irq.Length = sizeof(ACPI_RESOURCE);
        Resource->Irq.Data.Irq.Triggering = SelectedEntry->Trigger;
        Resource->Irq.Data.Irq.Polarity = SelectedEntry->Polarity;
        Resource->Irq.Data.Irq.Sharable = SelectedEntry->Shareable;
        Resource->Irq.Data.Irq.InterruptCount = 1;
        Resource->Irq.Data.Irq.Interrupts[0] = (UINT8)SelectedEntry->Irq;
    }
    else {
        Resource->Irq.Type = ACPI_RESOURCE_TYPE_EXTENDED_IRQ;
        Resource->Irq.Length = sizeof(ACPI_RESOURCE);
        Resource->Irq.Data.ExtendedIrq.ProducerConsumer = ACPI_CONSUMER;
        Resource->Irq.Data.ExtendedIrq.Triggering = SelectedEntry->Trigger;
        Resource->Irq.Data.ExtendedIrq.Polarity = SelectedEntry->Polarity;
        Resource->Irq.Data.ExtendedIrq.Sharable = SelectedEntry->Shareable;
        Resource->Irq.Data.ExtendedIrq.InterruptCount = 1;
        Resource->Irq.Data.ExtendedIrq.Interrupts[0] = (UINT8)SelectedEntry->Irq;
    }

    // Setup end-tag
    Resource->End.Type = ACPI_RESOURCE_TYPE_END_TAG;
    Resource->End.Length = sizeof(ACPI_RESOURCE);

    // Try to set current resource
    Status = AcpiSetCurrentResources(Source->Handle, &Buffer);
    if (ACPI_FAILURE(Status)) {
        ERROR("Failed to update the current irq resource, code %u", Status);
		return Status;
	}
    Source->ActiveEntry = SelectedEntry;
    
    // Make sure interrupt-manager knows about our allocation
    if (InterruptIncreasePenalty(SelectedEntry->Irq) != OsSuccess) {
        ERROR("Failed to increase penalty for irq");
    }
    
	// Get current source-handle status _STA
	Status = AcpiDeviceQueryStatus(Source->Handle, &DeviceStatus);
	// What now? @todo

    // No problems
    return AE_OK;
}

/* AcpiDeviceGetIrqRoutings
 * Utilizies ACPICA to retrieve all the irq-routings from
 * the ssdt information. */
ACPI_STATUS
AcpiDeviceGetIrqRoutings(
	_In_ AcpiDevice_t *Device)
{
	// Variables
	ACPI_PCI_ROUTING_TABLE *PciTable = NULL;
	PciRoutings_t *Table = NULL;
	ACPI_STATUS Status;
	int i;
    
    // Buffers
	IrqResource_t IrqResource;
    ACPI_BUFFER aBuff;

    // Debug
    TRACE("AcpiDeviceGetIrqRoutings()");

	// Setup a buffer for the routing table
	aBuff.Length = 0x2000;
    aBuff.Pointer = (char*)kmalloc(0x2000);
    memset(aBuff.Pointer, 0, 0x2000);

	// Try to get routings
	Status = AcpiGetIrqRoutingTable(Device->Handle, &aBuff);
	if (ACPI_FAILURE(Status)) {
        ERROR("Failed to extract irq routings, code %u", Status);
		goto done;
	}
	
	// Allocate a new table for the device
	Table = (PciRoutings_t*)kmalloc(sizeof(PciRoutings_t));
	Table->Sources = ListCreate(KeyString, LIST_NORMAL);
	for (i = 0; i < 128; i++) {
		Table->InterruptEntries[i] = NULL;
		Table->ActiveIrqs[i] = INTERRUPT_NONE;
	}

	// Store it in device
	Device->Routings = Table;

    // Enumerate entries
    PciTable = (ACPI_PCI_ROUTING_TABLE *)aBuff.Pointer;
    TRACE("Irq Table Length: %u", PciTable->Length);
	for (;PciTable->Length;
		 PciTable = (ACPI_PCI_ROUTING_TABLE *)((char *)PciTable + PciTable->Length)) {

		// Variabes
		PciRoutingSource_t *Source  = NULL;
		ACPI_HANDLE SourceHandle 	= NULL;
		unsigned InterruptIndex 	= 0;
		unsigned DeviceIndex 		= 0;
		ListNode_t *Node 			= NULL;
		DataKey_t Key;

        // Debug
        TRACE("0x%x:%u (Source[0]: %s, Irq %u)", 
            (unsigned)((PciTable->Address >> 16) & 0xFFFF), PciTable->Pin,
            (PciTable->Source[0] == '\0' ? "0" : &PciTable->Source[0]), PciTable->SourceIndex);

        // Convert the addresses 
        DeviceIndex = (unsigned)((PciTable->Address >> 16) & 0xFFFF);
        InterruptIndex = (DeviceIndex * 4) + PciTable->Pin;

		// Check if the first byte is 0, then there is no irq-resource
		// Then the SourceIndex is the actual IRQ
		if (PciTable->Source[0] == '\0') {
			PciRoutingEntry_t *RoutingEntry = NULL;

			if (Table->InterruptEntries[InterruptIndex] == NULL) {
				Table->InterruptEntries[InterruptIndex] = 
					ListCreate(KeyInteger, LIST_NORMAL);
			}

			// Allocate a new entry and store information
			RoutingEntry = (PciRoutingEntry_t*)kmalloc(sizeof(PciRoutingEntry_t));
            RoutingEntry->AcType = ACPI_RESOURCE_TYPE_IRQ;
			RoutingEntry->Irq = (int)PciTable->SourceIndex;
			RoutingEntry->Polarity = ACPI_ACTIVE_LOW;
			RoutingEntry->Trigger = ACPI_LEVEL_SENSITIVE;
			RoutingEntry->Fixed = 1;
			Key.Value = 0;

			// Save interrupt
			ListAppend(Table->InterruptEntries[InterruptIndex], 
				ListCreateNode(Key, Key, RoutingEntry));
			Table->ActiveIrqs[InterruptIndex] = (int)PciTable->SourceIndex;
			continue;
		}

		// Ok, so we have a valid handle, lets see if we already have
		// the handle cached in memory
		Key.String = &PciTable->Source[0];
		Node = ListGetNodeByKey(Table->Sources, Key, 0);
		if (Node != NULL) {
			Source = (PciRoutingSource_t*)Node->Data;
			Table->InterruptEntries[InterruptIndex] = Source->Entries;
			Table->ActiveIrqs[InterruptIndex] = Source->ActiveEntry->Irq;
			continue;
		}

		// Get handle of the source-table
		Status = AcpiGetHandle(Device->Handle, PciTable->Source, &SourceHandle);
		if (ACPI_FAILURE(Status)) {
			ERROR("Failed AcpiGetHandle\n");
			continue;
		}

		// Debug
		TRACE("Enumerating possible resources for a new source");
		Source = (PciRoutingSource_t*)kmalloc(sizeof(PciRoutingSource_t));
		Source->Handle = SourceHandle;
		Source->Entries = ListCreate(KeyInteger, LIST_NORMAL);
		Source->ActiveEntry = NULL;
		
		// Create the list for this new source
		Node = ListCreateNode(Key, Key, Source);
		ListAppend(Table->Sources, Node);

		// Store the information for the callback
		IrqResource.Gathering = 1;
		IrqResource.IrqList = Source->Entries;

		// Gather all possible irq's
		Status = AcpiWalkResources(SourceHandle, 
			METHOD_NAME__PRS, AcpiDeviceIrqRoutingCallback, &IrqResource);
		if (ACPI_FAILURE(Status)) {
			ERROR("Failed retrieving all possible irqs\n");
			continue;
		}

		// Debug
        TRACE("Enumerating current resources for handle");

        // Walk the handle and call all __CRS methods
		IrqResource.Gathering = 0;
		IrqResource.IrqActive = NULL;
		Status = AcpiWalkResources(SourceHandle, 
			METHOD_NAME__CRS, AcpiDeviceIrqRoutingCallback, &IrqResource);
		if (ACPI_FAILURE(Status)) {
			ERROR("Failed IRQ resource\n");
			continue;
		}

		// Update
		Source->ActiveEntry = IrqResource.IrqActive;

        // Select an irq and update the modifications
        Status = AcpiDeviceSelectIrq(Source);
		Table->InterruptEntries[InterruptIndex] = Source->Entries;
		if (Source->ActiveEntry != NULL) {
			Table->ActiveIrqs[InterruptIndex] = Source->ActiveEntry->Irq;
		}
	}

done:
	kfree(aBuff.Pointer);
	return Status;
}

/* AcpiDeviceGetHWInfo
 * Retrieves acpi-hardware information like Status, Address
 * CId's, HId, UId, CLS etc */
ACPI_STATUS
AcpiDeviceGetHWInfo(
    _InOut_ AcpiDevice_t *Device,
    _In_ ACPI_HANDLE ParentHandle,
    _In_ int Type)
{
    // Variables
	ACPI_PNP_DEVICE_ID_LIST *Cid = NULL;
	ACPI_DEVICE_INFO *DeviceInfo = NULL;
	ACPI_STATUS Status;
    const char *CidAdd = NULL;
	char *Hid = NULL;
	char *Uid = NULL;
    
    // Buffers
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

// AcpiDeviceInitialize
// Derive correct IRQ
// if HID == ACPI0006 then AcpiInstallGpeBlock
// if ACPI_FEATURE_PRW then AcpiSetWakeGpe
// AcpiUpdateAllGpes must be called afterwards

// AcpiDeviceDestroy