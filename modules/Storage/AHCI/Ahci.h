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
* TODO:
*	- Port Multiplier Support
*	- Power Management
*/

#ifndef _AHCI_H_
#define _AHCI_H_

/* Includes */
#include <os/osdefs.h>
#include <ds/list.h>

/* System Includes */
#include <DeviceManager.h>
#include <Semaphore.h>
#include <Module.h>

/* Include the Sata header */
#include <Sata.h>
#include <Ata.h>

/* AHCI Operation Registers */
#define AHCI_REGISTER_HOSTCONTROL		0x00
#define AHCI_REGISTER_VENDORSPEC		0xA0
#define AHCI_REGISTER_PORTBASE(Port)	(0x100 + (Port * 0x80))
#define AHCI_MAX_PORTS					32
#define AHCI_RECIEVED_FIS_SIZE			256

/* How much we should allocate for each port */
#define AHCI_PORT_PRDT_COUNT			32
#define AHCI_COMMAND_TABLE_SIZE			(128 + (16 * AHCI_PORT_PRDT_COUNT))
#define AHCI_PRDT_MAX_LENGTH			(4*1024*1024)

/* AHCI Generic Host Control Registers 
 * Global, apply to all AHCI ops */
#pragma pack(push, 1)
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
#pragma pack(pop)

/* AHCI Port Specific Registers
 * This is per port, which means these registers
 * only control a single port */
#pragma pack(push, 1)
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
#pragma pack(pop)

/* The Physical Region Descriptor Table 
 * Describes a scatter/gather list for data transfers 
 * Size, 16 bytes */
#pragma pack(push, 1)
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
#pragma pack(pop)

/* Interrupt Enable */
#define AHCI_PRDT_IOC			(1 << 31)

/* The command table, which is pointed to by a 
 * Command list header, and this table contains
 * a given number of FIS, 128 bytes */
#pragma pack(push, 1)
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
	AHCIPrdtEntry_t PrdtEntry[8];

} AHCICommandTable_t;
#pragma pack(pop)

/* The command list entry structure 
 * Contains a command for the port to execute */
#pragma pack(push, 1)
typedef struct _AHCICommandHeader
{
	/* Flags 
	 * Bits  0-4: Command FIS Length (CFL) 
	 * Bit     5: ATAPI 
	 * Bit     6: (1) Write, (0) Read
	 * Bit     7: Prefetchable (P)
	 * Bit     8: Reset, perform sync
	 * Bit     9: BIST Fis
	 * Bit    10: Clear Busy Upon R_OK 
	 * Bit    11: Reserved
	 * Bit 12-15: Port Multiplier Port */
	uint16_t Flags;

	/* Physical Region Descriptor Table Length */
	uint16_t TableLength;

	/* PRDBC: PRD Byte Count */
	uint32_t PRDByteCount;

	/* Command Table Base Address */
	uint32_t CmdTableBaseAddress;

	/* Command Table Base Address Upper */
	uint32_t CmdTableBaseAddressUpper;

	/* Reserved */
	uint32_t Reserved[4];

} AHCICommandHeader_t;
#pragma pack(pop)

/* The command list structure 
 * Contains a number of entries (1K /32 bytes)
 * for each port to execute */
#pragma pack(push, 1)
typedef struct _AHCICommandList
{
	/* The list, 32 entries */
	AHCICommandHeader_t Headers[32];

} AHCICommandList_t;
#pragma pack(pop)

/* Received FIS 
 * There are four kinds of FIS which may be sent to the host 
 * by the device as indicated in the following structure declaration */
#pragma pack(push, 1)
typedef volatile struct _AHCIFIS
{
	/* Offset 0x0 - Dma Setup FIS */
	FISDmaSetup_t DmaSetup;

	/* Padding */
	uint8_t Padding0[4];

	/* Offset 0x20 - Pio Setup FIS */
	FISPioSetup_t PioSetup;

	/* Padding, again */
	uint8_t Padding1[12];

	/* Offset 0x40 - Register – Device to Host FIS */
	FISRegisterD2H_t RegisterD2H;

	/* Padding, again again */
	uint8_t Padding2[4];

	/* Offset 0x58 - Set Device Bit FIS */
	FISDeviceBits_t	DeviceBits;

	/* Offset 0x60 - Space for unknown */
	uint8_t UnknownFIS[64];

	/* Offset 0xA0 - Reserved */
	uint8_t ReservedArea[0x100 - 0xA0];

} AHCIFis_t;
#pragma pack(pop)

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
#define AHCI_CAPABILITIES_NCS(Caps)			(((Caps >> 8) & 0x1F) + 1)

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

/* Bit 0: Start */
#define AHCI_PORT_ST						0x1

/* Bit 1: Spin-Up Device */
#define AHCI_PORT_SUD						0x2

/* Bit 2: Power On Device */
#define AHCI_PORT_POD						0x4

/* Bit 3: Command List Override */
#define AHCI_PORT_CLO						0x8

/* Bit 4: FIS Receive Enable */
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
#define AHCI_PORT_IE_PRCE					0x800000

/* Incorrect Port Multiplier Enable */
#define AHCI_PORT_IE_IPME					0x1000000

/* Overflow Enable */
#define AHCI_PORT_IE_OFE					0x2000000

/* Interface Non-fatal Error Enable */
#define AHCI_PORT_IE_INFE					0x4000000

/* Interface Fatal Error Enable */
#define AHCI_PORT_IE_IFE					0x8000000

/* Host Bus Data Error Enable */
#define AHCI_PORT_IE_HBDE					0x10000000

/* Host Bus Fatal Error Enable */
#define AHCI_PORT_IE_HBFE					0x20000000

/* Task File Error Enable */
#define AHCI_PORT_IE_TFEE					0x40000000

/* Cold Presence Detect Enable */
#define AHCI_PORT_IE_CPDE					0x80000000

/* Port x Task File Data (TaskFileData)
 * - Port Registers */

#define AHCI_PORT_TFD_ERR					0x1
#define AHCI_PORT_TFD_DRQ					0x8
#define AHCI_PORT_TFD_BSY					0x80

/* Port Ata Control (AtaControl)
 * - Port Registers */

/* Device Detection Initialization */
#define AHCI_PORT_SCTL_DET_MASK				0xF
#define AHCI_PORT_SCTL_DET_RESET			0x1
#define AHCI_PORT_SCTL_DET_DISABLE			0x4

/* Port Ata Status (AtaStatus)
 * - Port Registers */

/* Device Detection */
#define AHCI_PORT_STSS_DET(Sts)				(Sts & 0xF)
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
	/* Id & Index */
	int Id;
	int Index;

	/* Whether or not this port 
	 * has a device connected */
	int Connected;

	/* Register Access for this port */
	volatile AHCIPortRegisters_t *Registers;

	/* Memory resources */
	AHCICommandList_t *CommandList;
	AHCIFis_t **RecievedFisTable;
	AHCIFis_t *RecievedFis;
	void *CommandTable;

	/* Status of command slots
	 * There can be max 32 slots,
	 * so we use a 32 bit unsigned */
	uint32_t SlotStatus;

	/* Port lock/queue */
	Semaphore_t **SlotQueues;
	Spinlock_t Lock;

	/* Transactions for this port 
	 * Keeps track of active transfers. 
	 * Key -> Slot, SubKey -> Multiplier */
	List_t *Transactions;

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

	/* Number of command slots for ports 
	 * So we can allocate stuff correctly */
	size_t CmdSlotCount;

	/* Shared resource bases 
	 * Especially command lists for optimizing memory */
	void *CmdListBase;
	void *FisBase;
	void *CmdTableBase;

} AhciController_t;

/* The AHCI Device Structure 
 * This describes an attached ahci device 
 * and the information neccessary to deal with it */
#pragma pack(push, 1)
typedef struct _AhciDevice
{
	/* First of all, we need 
	 * to know which controller and port 
	 * this device belongs to */
	AhciController_t *Controller;
	AhciPort_t *Port;

	/* Next up we describe the capabilities 
	 * DeviceType:
	 * 0 -> ATA
	 * 1 -> ATAPI */
	int DeviceType;

	/* Whether or not this device supports
	 * DMA transfers, otherwise fallback to PIO */
	int UseDMA;

	/* Addressing mode is descriped as 
	 * (0) CHS, (1) LBA28, (2) LBA48 */
	int AddressingMode;

	/* Sector Count, implemented as both LBA28 and
	 * LBA48 to keep it simple, see addressing mode */
	uint64_t SectorsLBA;

	/* Size of a physical sector in bytes 
	 * Calculated from the IDENTIFY command */
	size_t SectorSize;

} AhciDevice_t;
#pragma pack(pop)

/* AHCIPortCreate
 * Initializes the port structure, but not memory structures yet */
_CRT_EXTERN AhciPort_t *AhciPortCreate(AhciController_t *Controller, int Port, int Index);

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

/* AHCIPortAcquireCommandSlot
 * Allocates an available command slot on a port
 * returns index on success, otherwise -1 */
_CRT_EXTERN int AhciPortAcquireCommandSlot(AhciController_t *Controller, AhciPort_t *Port);

/* AHCIPortReleaseCommandSlot
 * Deallocates a previously allocated command slot */
_CRT_EXTERN void AhciPortReleaseCommandSlot(AhciPort_t *Port, int Slot);

/* AHCIPortStartCommandSlot
 * Starts a command slot on the given port */
_CRT_EXTERN void AhciPortStartCommandSlot(AhciPort_t *Port, int Slot);

/* AHCIPortInterruptHandler
 * Port specific interrupt handler 
 * handles interrupt for a specific port */
_CRT_EXTERN void AhciPortInterruptHandler(AhciController_t *Controller, AhciPort_t *Port);

/* AHCIDeviceIdentify 
 * Identifies the device and type on a port
 * and sets it up accordingly */
_CRT_EXTERN void AhciDeviceIdentify(AhciController_t *Controller, AhciPort_t *Port);



#endif //!_AHCI_H_