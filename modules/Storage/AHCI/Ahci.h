/* MollenOS
*
* Copyright 2011 - 2014, Philip Meulengracht
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
* MollenOS MCore - Advanced Host Controller Interface Driver
*/

#ifndef _AHCI_H_
#define _AHCI_H_

/* Includes */
#include <os/osdefs.h>

/* AHCI Operation Registers */
#define AHCI_REGISTER_HOSTCONTROL		0x00
#define AHCI_REGISTER_VENDORSPEC		0xA0
#define AHCI_REGISTER_PORTBASE(Port)	(0x100 + (Port * 0x80))


/* AHCI Generic Host Control Registers 
 * Global, apply to all AHCI ops */
typedef struct _AHCIGenericRegisters
{
	/* Host Capabilities */
	uint32_t Capabilities;

	/* Global Host Control */
	uint32_t GlobalHostControl;

	/* Interrupt Status */
	uint32_t InterruptStatus;

	/* Ports Implemented */
	uint32_t PortsImplemented;

	/* Version */
	uint32_t Version;

	/* Command Completion 
	 * - Coalescing Control */
	uint32_t CcControl;

	/* Command Completion
	 * - Coalescing Ports */
	uint32_t CcPorts;

	/* Enclosure Management 
	 * - Location */
	uint32_t EmLocation;

	/* Enclosure Management 
	 * - Control */
	uint32_t EmControl;

	/* Host Capabilities 
	 * - Extended */
	uint32_t CapabilitiesExtended;

	/* BIOS/OS Handoff Register */
	uint32_t OSControlAndStatus;

} AHCIGenericRegisters_t;

/* AHCI Port Specific Registers
 * This is per port, which means these registers
 * only control a single port */
typedef struct _AHCIPortRegisters
{
	/* Command List Base Address */
	uint32_t CmdListBaseAddress;

	/* Command List Base Address Upper 32-Bits */
	uint32_t CmdListBaseAddressUpper;

	/* FIS Base Address */
	uint32_t FISBaseAddress;

	/* FIS Base Address Upper 32-Bits */
	uint32_t FISBaseAdressUpper;

	/* Interrupt Status */
	uint32_t InterruptStatus;

	/* Interrupt Enable */
	uint32_t InterruptEnable;

	/* Command and Status */
	uint32_t CommandAndStatus;

	/* Reserved Register */
	uint32_t Reserved;

	/* Task File Data */
	uint32_t TaskFileData;

	/* Signature */
	uint32_t Signature;

	/* Serial ATA Status 
	 * - (SCR0: SStatus) */
	uint32_t AtaStatus;

	/* Serial ATA Control 
	 * - (SCR2: SControl) */
	uint32_t AtaControl;

	/* Serial ATA Error 
	 * - (SCR1: SError) */
	uint32_t AtaError;

	/* Serial ATA Active 
	 * - (SCR3: SActive) */
	uint32_t AtaActive;

	/* Command Issue */
	uint32_t CommandIssue;

	/* Serial ATA Notification 
	 * - (SCR4: SNotification) */
	uint32_t AtaNotification;

	/* FIS-based Switching Control */
	uint32_t FISControl;

	/* Device Sleep */
	uint32_t DeviceSleep;

	/* Reserved Area */
	uint32_t ReservedArea[10];

	/* Vendor Specific Registers
	 * 16 Bytes, 4 registers */
	uint32_t VendorSpecifics[4];

} AHCIPortRegisters_t ;

/* The command list entry structure 
 * Contains a command for the port to execute */
typedef struct _AHCICommandHeader
{
	/* Command Description Information */
	uint32_t Description;

	/* Command Status */
	uint32_t Status;

	/* Command Table Base Address */
	uint32_t CmdTableBaseAddress;

	/* Command Table Base Address Upper */
	uint32_t CmdTableBaseAddressUpper;

	/* Reserved */
	uint32_t Reserved[4];

} AHCICommandHeader_t;

/* The command list structure 
 * Contains a number of entries (1K /32 bytes)
 * for each port to execute */
typedef struct _AHCICommandList
{
	/* The list, 32 entries */
	AHCICommandHeader_t Headers[32];

} AHCICommandTable_t;

/* Capability Bits (Host Capabilities) 
 * - Generic Registers */

/* Number of ports */
#define AHCI_CAPABILITIES_NP(Caps)			((Caps & 0x1F) + 1)

/* Supports External SATA */
#define AHCI_CAPABILITIES_SXS				0x20

/* Supports Enclosure Management */
#define AHCI_CAPABILITIES_EMS				0x40

/* Supports Command Completion Coalescing */
#define AHCI_CAPABILITIES_CCCS				0x80

/* Number of Command Slots */
#define AHCI_CAPABILITIES_NCS(Caps)			(((Caps & 0x1F00) >> 8) + 1)

/* Partial State Capable */
#define AHCI_CAPABILITIES_PSC				0x2000

/* Slumber State Capable */
#define AHCI_CAPABILITIES_SSC				0x4000

/* PIO Multiple DRQ Block */
#define AHCI_CAPABILITIES_PMD				0x8000

/* FIS-based Switching Supported */
#define AHCI_CAPABILITIES_FBSS				0x10000

/* Supports Port Multiplier */
#define AHCI_CAPABILITIES_SPM				0x20000

/* Supports AHCI mode only */
#define AHCI_CAPABILITIES_SAM				0x40000

/* Interface Speed Support */
#define AHCI_CAPABILITIES_ISS(Caps)			((Caps & 0xF00000) >> 20)
#define AHCI_SPEED_1_5GBPS					0x1
#define AHCI_SPEED_3_0GBPS					0x2
#define AHCI_SPEED_6_0GBPS					0x3

/* Supports Command List Override */
#define AHCI_CAPABILITIES_SCLO				0x1000000

/* Supports Activity LED */
#define AHCI_CAPABILITIES_SAL				0x2000000

/* Supports Aggressive Link Power Management */
#define AHCI_CAPABILITIES_SALP				0x4000000

/* Supports Staggered Spin-up */
#define AHCI_CAPABILITIES_SSS				0x8000000

/* Supports Mechanical Presence Switch */
#define AHCI_CAPABILITIES_SMPS				0x10000000

/* Supports SNotification Register */
#define AHCI_CAPABILITIES_SSNTF				0x20000000

/* Supports Native Command Queuing */
#define AHCI_CAPABILITIES_SNCQ				0x40000000

/* Supports 64-bit Addressing */
#define AHCI_CAPABILITIES_S64A				0x80000000

/* Port Interrupt Bits (InterruptStatus)
 * - Generic Registers */
#define AHCI_INTERRUPT_PORT(Port)			(1 << Port)

/* Ports Implemented (PortsImplemented)
 * - Generic Registers */
#define AHCI_IMPLEMENTED_PORT(Port)			(1 << Port)

/* AHCI Version (Version)
 * - Generic Registers */
#define AHCI_VERSION_095					0x00000905
#define AHCI_VERSION_100					0x00010000
#define AHCI_VERSION_110					0x00010100
#define AHCI_VERSION_120					0x00010200
#define AHCI_VERSION_130					0x00010300
#define AHCI_VERSION_131					0x00010301

/* Capability Bits Extended (Host Capabilities Extended)
 * - Generic Registers */

/* BIOS/OS Handoff */
#define AHCI_XCAPABILITIES_OS_HANDOFF		0x1

/* NVMHCI Present */
#define AHCI_XCAPABILITIES_NVMP				0x2

/* Automatic Partial to Slumber Transitions */
#define AHCI_XCAPABILITIES_APST				0x4

/* Supports Device Sleep */
#define AHCI_XCAPABILITIES_SDS				0x8

/* Supports Aggressive Device Sleep Management */
#define AHCI_XCAPABILITIES_SADM				0x10

/* DevSleep Entrance from Slumber Only */
#define AHCI_XCAPABILITIES_DESO				0x20

/* BIOS/OS Handoff Control and Status (OSControlAndStatus)
 * - Generic Registers */

/* BIOS Owned Semaphore */
#define AHCI_CONTROLSTATUS_BOS				0x1

/* OS Owned Semaphore */
#define AHCI_CONTROLSTATUS_OOS				0x2

/* SMI on OS Ownership Change Enable */
#define AHCI_CONTROLSTATUS_SOOE				0x4

/* OS Ownership Change */
#define AHCI_CONTROLSTATUS_OOC				0x8

/* BIOS Busy */
#define AHCI_CONTROLSTATUS_BB				0x10

#endif //!_AHCI_H_