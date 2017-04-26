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
 * MollenOS MCore - ACPI Support Definitions & Structures
 * - This header describes the base acpi-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef _ACPI_INTEFACE_H_
#define _ACPI_INTEFACE_H_

/* Includes
 * - System */
#include <os/osdefs.h>

/* Values for description table header signatures for tables defined in this
 * file. Useful because they make it more difficult to inadvertently type in
 * the wrong signature. */
#ifndef __ACPI_EXCLUDE_TABLES
#include <os/driver/acpi/acsetup.h>
#include <os/driver/acpi/actypes.h>
#include <os/driver/acpi/actbl.h>
#include <os/driver/acpi/actbl1.h>
#include <os/driver/acpi/actbl2.h>
#include <os/driver/acpi/actbl3.h>
#endif

/* The ACPI system descriptor structure
 * It contains basic information about the
 * acpi-availability on the current system
 * together with a small subset of frequently used data */
typedef struct _AcpiDescriptor {
	uint8_t			Version;		/* Version of the ACPI on the current platform */
	uint8_t			Century;		/* Index to century in RTC CMOS RAM */
	uint16_t		BootFlags;		/* IA-PC Boot Architecture Flags */
	uint16_t		ArmBootFlags;	/* ARM-Specific Boot Flags (ACPI 5.1) */
} AcpiDescriptor_t;

/* Version definitions for the version field
 * in the above structure */
#define ACPI_VERSION_1_0			(0x10)
#define ACPI_VERSION_2_0			(0x20)
#define ACPI_VERSION_3_0			(0x30)
#define ACPI_VERSION_4_0			(0x40)
#define ACPI_VERSION_5_0			(0x50)
#define ACPI_VERSION_5_1			(0x51)
#define ACPI_VERSION_6_0			(0x60)

/* Masks for FADT IA-PC Boot Architecture Flags 
 * (BootFlags) [Vx]=Introduced in this FADT revision */
#define ACPI_IA_LEGACY_DEVICES		(1)         /* 00: [V2] System has LPC or ISA bus devices */
#define ACPI_IA_8042				(1<<1)      /* 01: [V3] System has an 8042 controller on port 60/64 */
#define ACPI_IA_NO_VGA				(1<<2)      /* 02: [V4] It is not safe to probe for VGA hardware */
#define ACPI_IA_NO_MSI				(1<<3)      /* 03: [V4] Message Signaled Interrupts (MSI) must not be enabled */
#define ACPI_IA_NO_ASPM				(1<<4)      /* 04: [V4] PCIe ASPM control must not be enabled */
#define ACPI_IA_NO_CMOS_RTC			(1<<5)      /* 05: [V5] No CMOS real-time clock present */

/* Masks for FADT ARM Boot Architecture Flags 
 * (ArmBootFlags) ACPI 5.1 */
#define ACPI_ARM_PSCI_COMPLIANT		(1)         /* 00: [V5+] PSCI 0.2+ is implemented */
#define ACPI_ARM_PSCI_USE_HVC		(1<<1)      /* 01: [V5+] HVC must be used instead of SMC as the PSCI conduit */

/* AcpiQueryStatus
 * Queries basic acpi information and returns either OsSuccess
 * or OsError if Acpi is not supported on the running platform */
MOSAPI OsStatus_t AcpiQueryStatus(AcpiDescriptor_t *AcpiDescriptor);

/* AcpiQueryTable
 * Queries the full table information of the table that matches
 * the given signature, and copies the information to the supplied pointer
 * the buffer is automatically allocated, and should be cleaned up afterwards  */
MOSAPI OsStatus_t AcpiQueryTable(const char *Signature, ACPI_TABLE_HEADER **Table);

/* AcpiQueryInterrupt
 * Queries the interrupt-line for the given bus, device and
 * pin combination. The pin must be zero indexed. Conform flags
 * are returned in the <AcpiConform> */
MOSAPI OsStatus_t AcpiQueryInterrupt(DevInfo_t Bus, DevInfo_t Device, int Pin,
	int *Interrupt, Flags_t *AcpiConform);

#endif //!_ACPI_INTEFACE_H_
