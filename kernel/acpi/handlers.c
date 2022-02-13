/**
 * MollenOS
 *
 * Copyright 2018, Philip Meulengracht
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
 * ACPI(CA) System Interface
 *  - Implementation for acpi event handlers like power events.
 */
#define __MODULE "ACPI"
#define __TRACE

#include <acpiinterface.h>
#include <assert.h>
#include <debug.h>

UINT32 AcpiShutdownHandler(void *Context)
{
	ACPI_EVENT_STATUS eStatus;
	ACPI_STATUS Status;

	/* Get Event Data */
	Status = AcpiGetEventStatus(ACPI_EVENT_POWER_BUTTON, &eStatus);

	/* Sanity */
	assert(ACPI_SUCCESS(Status));

	/* */
	if (eStatus & ACPI_EVENT_FLAG_ENABLED)
	{
		/* bla bla */
		AcpiClearEvent(ACPI_EVENT_POWER_BUTTON);
	}

	/* Shutdown - State S5 */
	AcpiEnterSleepState(ACPI_STATE_S5);

	return AE_OK;
}

UINT32 AcpiSleepHandler(void *Context)
{
	//AcpiEnterSleepState
	return AE_OK;
}

UINT32 AcpiRebootHandler(void)
{
	ACPI_STATUS status = AcpiReset();

	if (ACPI_FAILURE(status))
		TRACE("Reboot is unsupported\n");
	else
		TRACE("Reboot is in progress...\n");
	for (;;);
}

/* AcpiBusNotifyHandler
 * Global system notification handler for generic device events on the ACPI bus. */
void
AcpiBusNotifyHandler(
	ACPI_HANDLE Device,
	UINT32 NotifyType,
	void *Context)
{
	_CRT_UNUSED(Device);
	_CRT_UNUSED(Context);
	ERROR("Global Notification: Type 0x%" PRIxIN "\n", NotifyType);
}

/* AcpiEventHandler
 * Global event handler for ACPI devices and the 
 * generic events they produce. */
void
AcpiEventHandler(
    UINT32                          EventType,
    ACPI_HANDLE                     Device,
    UINT32                          EventNumber,
    void                            *Context)
{
    // We don't support the global events yet..
	_CRT_UNUSED(Device);
    _CRT_UNUSED(Context);
    
    // Trace
    if (EventType == ACPI_EVENT_TYPE_GPE) {
        DEBUG("ACPI Gpe Event - 0x%" PRIxIN "", EventNumber);
    }
    else if (EventType == ACPI_EVENT_TYPE_FIXED) {
        DEBUG("ACPI Fixed Event - 0x%" PRIxIN "", EventNumber);
    }
    else {
        FATAL(FATAL_SCOPE_KERNEL, "Invalid ACPI Global Event %" PRIuIN "", EventType);
    }
}