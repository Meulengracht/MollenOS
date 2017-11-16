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
 * MollenOS x86 Common Timer Interface
 * - Contains shared x86 timer routines
 */
#define __MODULE "TMIF"
#define __TRACE

/* Includes
 * - System */
#include <acpiinterface.h>
#include <debug.h>
#include "../cmos.h"
#include "../pit.h"

/* TimersDiscover 
 * Discover the available system timers for the x86 platform. */
OsStatus_t
TimersDiscover(void)
{
    // Variables
	ACPI_TABLE_HEADER *Header   = NULL;
    int BootTimers              = 1;
    int RtcAvailable            = 1;

    // Debug
    TRACE("TimersDiscover()");

    // Start out by detecting presence of HPET
    if (AcpiAvailable() == ACPI_AVAILABLE) {
        if (ACPI_SUCCESS(AcpiGetTable(ACPI_SIG_HPET, 0, &Header))) {
            TRACE("Not initializing any timers as hpet is present");
            BootTimers = 0;
        }
        if (AcpiGbl_FADT.BootFlags & ACPI_FADT_NO_CMOS_RTC) {
            RtcAvailable = 0;
        }
    }
	
    // Start timers?
    if (BootTimers == 0) {
        // Nope, start only clock
        RtcAvailable = 0;
    }
    else {
        // Do we have an RTC?
        if (RtcAvailable == 0) {
            if (PitInitialize() != OsSuccess) {
                ERROR("Failed to initialize the PIT.");
            }
        }
    }
    return CmosInitialize(RtcAvailable);
}