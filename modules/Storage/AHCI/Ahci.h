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
#include <Module.h>
#include <DeviceManager.h>

/* AHCI Operation Registers */
#define AHCI_REGISTER_HOSTCONTROL		0x00
#define AHCI_REGISTER_VENDORSPEC		0xA0
#define AHCI_REGISTER_PORTBASE(Port)	(0x100 + (Port * 0x80))
#define AHCI_MAX_PORTS					32

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

/* The SATA specs specify these kinds of 
 * FIS (Frame Information Structure) */
typedef enum _AHCIFisType
{
	/* Register FIS - Host To Device */
	FIS_TYPE_REG_H2D = 0x27,
	
	/* Register FIS - Device To Host */
	FIS_TYPE_REG_D2H = 0x34,

	/* DMA Activate FIS - Device To Host */
	FIS_TYPE_DMA_ACT = 0x39,

	/* DMA Setup FIS - Bidirectional */
	FIS_TYPE_DMA_SETUP = 0x41,

	/* Data FIS - Bidirectional */
	FIS_TYPE_DATA = 0x46,

	/* BIST Activate FIS - Bidirectional */
	FIS_TYPE_BIST = 0x58,

	/* PIO Setup FIS - Device To Host */
	FIS_TYPE_PIO_SETUP = 0x5F,

	/* Set device bits FIS - Device To Host */
	FIS_TYPE_DEV_BITS = 0xA1

} AHCIFisType_t;

/* The Physical Region Descriptor Table 
 * Describes a scatter/gather list for data transfers */
typedef struct _AHCIPrdtEntry
{
	/* Data Base Address */
	uint32_t DataBaseAddress;

	/* Data Base Address Upper */
	uint32_t DataBaseAddressUpper;

	/* Reserved */
	uint32_t Reserved;

	/* Descriptor Information 
	 * Bits 00-21: Data Byte Count 
	 * Bit 31: Interrupt on Completion */
	uint32_t Descriptor;

} AHCIPrdtEntry_t;

/* The command table, which is pointed to by a 
 * Command list header, and this table contains
 * a given number of FIS */
typedef struct _AHCICommandTable
{
	/* The first 64 bytes are reserved 
	 * for a command fis */
	uint8_t FISCommand[64];

	/* The next 16 bytes are reserved
	 * for an atapi fis */
	uint8_t FISAtapi[16];

	/* The next 48 bytes are reserved */
	uint8_t Reserved[48];

	/* Between 0...65535 entries  
	 * of PRDT */
	AHCIPrdtEntry_t PrdtEntry[1];

} AHCICommandTable_t;

/* The command list entry structure 
 * Contains a command for the port to execute */
typedef struct _AHCICommandHeader
{
	/* Physical Region Descriptor */
	uint32_t Descriptor;

	/* PRDBC: PRD Byte Count */
	uint32_t PRDByteCount;

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

} AHCICommandList_t;

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

/* Global HBA Control (GlobalHostControl)
 * - Generic Registers */

/* HBA Reset */
#define AHCI_HOSTCONTROL_HR					0x1

/* Global Interrupt Enable */
#define AHCI_HOSTCONTROL_IE					0x2

/* MSI Revert to Single Message */
#define AHCI_HOSTCONTROL_MRSM				0x4

/* AHCI Enable */
#define AHCI_HOSTCONTROL_AE					0x80000000

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

/* Port Control & Status (CommandAndStatus)
* - Port Registers */

/* Start */
#define AHCI_PORT_ST						0x1

/* Spin-Up Device */
#define AHCI_PORT_SUD						0x2

/* Power On Device */
#define AHCI_PORT_POD						0x4

/* Command List Override */
#define AHCI_PORT_CLO						0x8

/* FIS Receive Enable */
#define AHCI_PORT_FRE						0x10

/* Current Command Slot */
#define AHCI_PORT_CCS(Register)				((Register >> 8) & 0x1F)

/* Mechanical Presence Switch State */
#define AHCI_PORT_MPSS						0x2000

/* FIS Receive Running */
#define AHCI_PORT_FR						0x4000

/* Command List Running */
#define AHCI_PORT_CR						0x8000

/* Cold Presence State */
#define AHCI_PORT_CPS						0x10000

/* Port Multiplier Attached */
#define AHCI_PORT_PMA						0x20000

/* Hot Plug Capable Port */
#define AHCI_PORT_HPCP						0x40000

/* Mechanical Presence Switch Attached to Port */
#define AHCI_PORT_MPSP						0x80000

/* Cold Presence Detection */
#define AHCI_PORT_CPD						0x100000

/* External SATA Port */
#define AHCI_PORT_ESP						0x200000

/* FIS-based Switching Capable Port */
#define AHCI_PORT_FBSCP						0x400000

/* Automatic Partial to Slumber Transitions Enabled */
#define AHCI_PORT_APSTE						0x800000

/* Device is ATAPI */
#define AHCI_PORT_ATAPI						0x1000000

/* Drive LED on ATAPI Enable */
#define AHCI_PORT_DLAE						0x2000000

/* Aggressive Link Power Management Enable */
#define AHCI_PORT_ALPE						0x4000000

/* Aggressive Slumber / Partial */
#define AHCI_PORT_ASP						0x8000000

/* Interface Communication Control */
#define AHCI_PORT_ICC(Register)				((Register >> 28) & 0xF)
#define AHCI_PORT_ICC_SET(Register, Mode)	Register |= ((Mode & 0xF) << 28)
#define AHCI_PORT_ICC_IDLE					0x0
#define AHCI_PORT_ICC_ACTIVE				0x1
#define AHCI_PORT_ICC_PARTIAL				0x2
#define AHCI_PORT_ICC_SLUMBER				0x6
#define AHCI_PORT_ICC_DEVSLEEP				0x8

/* Port x Interrupt Enable (InterruptEnable)
 * - Port Registers */

/* Device to Host Register FIS Interrupt Enable */
#define AHCI_PORT_IE_DHRE					0x1

/* PIO Setup FIS Interrupt Enable */
#define AHCI_PORT_IE_PSE					0x2

/* DMA Setup FIS Interrupt Enable */
#define AHCI_PORT_IE_DSE					0x4

/* Set Device Bits FIS Interrupt Enable */
#define AHCI_PORT_IE_SDBE					0x8

/* Unknown FIS Interrupt Enable */
#define AHCI_PORT_IE_UFE					0x10

/* Descriptor Processed Interrupt Enable */
#define AHCI_PORT_IE_DPE					0x20

/* Port Change Interrupt Enable */
#define AHCI_PORT_IE_PCE					0x40

/* Device Mechanical Presence Enable */
#define AHCI_PORT_IE_DMPE					0x80

/* PhyRdy Change Interrupt Enable */
#define AHCI_PORT_IE_PRCE					(1 << 22)

/* Incorrect Port Multiplier Enable */
#define AHCI_PORT_IE_IPME					(1 << 23)

/* Overflow Enable */
#define AHCI_PORT_IE_OFE					(1 << 24)

/* Interface Non-fatal Error Enable */
#define AHCI_PORT_IE_INFE					(1 << 26)

/* Interface Fatal Error Enable */
#define AHCI_PORT_IE_IFE					(1 << 27)

/* Host Bus Data Error Enable */
#define AHCI_PORT_IE_HBDE					(1 << 28)

/* Host Bus Fatal Error Enable */
#define AHCI_PORT_IE_HBFE					(1 << 29)

/* Task File Error Enable */
#define AHCI_PORT_IE_TFEE					(1 << 30)

/* Cold Presence Detect Enable */
#define AHCI_PORT_IE_CPDE					(1 << 31)


/* Port Ata Control (AtaControl)
 * - Port Registers */

/* Device Detection Initialization */
#define AHCI_PORT_SCTL_DET_MASK				0xF
#define AHCI_PORT_SCTL_DET_RESET			0x1
#define AHCI_PORT_SCTL_DET_DISABLE			0x4

/* Port Ata Status (AtaStatus)
 * - Port Registers */

/* Device Detection */
#define AHCI_PORT_SSTS_DET_NODEVICE			0x0
#define AHCI_PORT_SSTS_DET_NOPHYCOM			0x1 /* Device is present, but no phys com */
#define AHCI_PORT_SSTS_DET_ENABLED			0x3 

/* Port Ata Error (AtaError)
 * - Port Registers */

/* Helpers */
#define AHCI_PORT_SERR_CLEARALL				0x3FF783

/* The AHCI Controller Port 
 * Contains all memory structures neccessary
 * for port transactions */
typedef struct _AhciPort
{
	/* Id */
	int Id;

	/* Register Access for this port */
	volatile AHCIPortRegisters_t *Registers;

	/* PRDT, FIS */

} AhciPort_t;

/* The AHCI Controller 
 * It contains all information neccessary 
 * for us to use it for our functions */
typedef struct _AhciController
{
	/* Id */
	int Id;

	/* Device */
	MCoreDevice_t *Device;

	/* Lock */
	Spinlock_t Lock;

	/* Registers */
	volatile AHCIGenericRegisters_t *Registers;

	/* Ports */
	AhciPort_t *Ports[AHCI_MAX_PORTS];
	uint32_t ValidPorts;

} AhciController_t;

/* AHCISetup
 * Initializes memory structures, ports and 
 * resets the controller so it's ready for use */
_CRT_EXTERN void AhciSetup(AhciController_t *Controller);

/* AHCIPortCreate
 * Initializes the port structure, but not memory structures yet */
_CRT_EXTERN AhciPort_t *AhciPortCreate(AhciController_t *Controller, int Port);

/* AHCIPortCleanup
 * Destroys a port, cleans up device, cleans up memory and resources */
_CRT_EXTERN void AhciPortCleanup(AhciController_t *Controller, AhciPort_t *Port);

/* AHCIPortInit
 * Initializes the memory regions and enables them in the port */
_CRT_EXTERN void AhciPortInit(AhciController_t *Controller, AhciPort_t *Port);

/* AHCIPortSetupDevice
 * Identifies connection on a port, and initializes connection/device */
_CRT_EXTERN void AhciPortSetupDevice(AhciController_t *Controller, AhciPort_t *Port);

/* AHCIPortReset
 * Resets the port, and resets communication with the device on the port
 * if the communication was destroyed */
_CRT_EXTERN OsStatus_t AhciPortReset(AhciController_t *Controller, AhciPort_t *Port);

#endif //!_AHCI_H_