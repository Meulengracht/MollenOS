/**
 * MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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
 * Advanced Host Controller Interface Driver
 * TODO:
 *    - Port Multiplier Support
 *    - Power Management
 */

#ifndef _AHCI_H_
#define _AHCI_H_

#include <ddk/busdevice.h>
#include <ddk/storage.h>
#include <ddk/interrupt.h>
#include <ds/list.h>
#include <os/osdefs.h>
#include <os/spinlock.h>
#include <os/dmabuf.h>

// SATA includes
#include <commands.h>
#include <sata.h>

/* AHCI Operation Registers */
#define AHCI_REGISTER_HOSTCONTROL       0x00
#define AHCI_REGISTER_VENDORSPEC        0xA0
#define AHCI_REGISTER_PORTBASE(Port)    (0x100 + (Port * 0x80))
#define AHCI_MAX_PORTS                  32
#define AHCI_RECIEVED_FIS_SIZE          256

PACKED_TYPESTRUCT(AHCIGenericRegisters, {
    reg32_t                Capabilities;
    reg32_t                GlobalHostControl;
    reg32_t                InterruptStatus;
    reg32_t                PortsImplemented;
    reg32_t                Version;
    
    reg32_t                CcControl;           // Coalescing Control
    reg32_t                CcPorts;             // Coalescing Ports
    
    reg32_t                EmLocation;          // Enclosure Management Location
    reg32_t                EmControl;           // Enclosure Management Control
    
    reg32_t                CapabilitiesExtended;
    reg32_t                OSControlAndStatus;
});

PACKED_TYPESTRUCT(AHCIPortRegisters, {
    reg32_t                CmdListBaseAddress;       // 1K Aligned
    reg32_t                CmdListBaseAddressUpper;
    reg32_t                FISBaseAddress;
    reg32_t                FISBaseAdressUpper;
    
    reg32_t                InterruptStatus;
    reg32_t                InterruptEnable;
    reg32_t                CommandAndStatus;
    reg32_t                Reserved;
    reg32_t                TaskFileData;
    reg32_t                Signature;
    
    reg32_t                STSS;                // (SCR0: SStatus)
    reg32_t                SCTL;                // (SCR2: SControl)
    reg32_t                SERR;                // (SCR1: SError)
    reg32_t                SACT;                // (SCR3: SActive)

    reg32_t                CI;                  // CommandIssue
    reg32_t                SNTF;                // (SCR4: SNotification)
    
    reg32_t                FISControl;
    reg32_t                DeviceSleep;
    reg32_t                ReservedArea[10];
    reg32_t                VendorSpecifics[4];  // 16 Bytes, 4 registers
});

PACKED_TYPESTRUCT(AHCIPrdtEntry, {
    reg32_t DataBaseAddress;       // Must be word aligned, bit 0 MUST be clear
    reg32_t DataBaseAddressUpper;
    reg32_t Reserved;

    /**
     * Descriptor Information
     * Bits 00-21: Data Byte Count 
     * Bit 31: Interrupt on Completion
     */
    reg32_t Descriptor;
});
#define AHCI_PRDT_IOC (1 << 31)  // Interrupt on Completion

#define AHCI_COMMAND_TABLE_PRDT_SIZE    16 // == sizeof(AHCIPrdtEntry_t)
#define AHCI_PRDT_MAX_LENGTH            (4 * 1024 * 1024) // Max number of bytes per PRDT

// Contains a FIS-Command and a bunch of data-descriptors [AHCIPrdtEntry_t]
PACKED_TYPESTRUCT(AHCICommandTable, {
    uint8_t         FISCommand[64];
    uint8_t         FISAtapi[16];
    uint8_t         Reserved[48];
    AHCIPrdtEntry_t PrdtEntry[1];    // Between 0...65535 entries
});

#define AHCI_COMMAND_TABLE_HEADER_SIZE  128 // == sizeof(AHCICommandTable_t)
#define AHCI_COMMAND_TABLE_PRDT_COUNT   ((4096 - AHCI_COMMAND_TABLE_HEADER_SIZE) / AHCI_COMMAND_TABLE_PRDT_SIZE)
#define AHCI_COMMAND_TABLE_SIZE         4096 // Use an entire page of memory on 4kb systems

/* The command list entry structure 
 * Contains a command for the port to execute */
PACKED_TYPESTRUCT(AHCICommandHeader, {
    // Flags 
    // Bits  0-4: Command FIS Length (CFL) 
    // Bit     5: ATAPI 
    // Bit     6: (1) Write, (0) Read
    // Bit     7: Prefetchable (P)
    // Bit     8: Reset, perform sync
    // Bit     9: BIST Fis
    // Bit    10: Clear Busy Upon R_OK 
    // Bit    11: Reserved
    // Bit 12-15: Port Multiplier Port
    uint16_t Flags;
    uint16_t TableLength;    // (PRDT) Physical Region Descriptor Table Length
    uint32_t PRDByteCount;   // PRDBC: PRD Byte Count

    uint32_t CmdTableBaseAddress;      // 128 Bytes Alignment
    uint32_t CmdTableBaseAddressUpper;            
    uint32_t Reserved[4];
});

/* The command list structure 
 * Contains a number of entries (1K /32 bytes) for each port to execute */
PACKED_TYPESTRUCT(AHCICommandList, {
    AHCICommandHeader_t Headers[32];
});

/**
 * Received FIS
 * There are four kinds of FIS which may be sent to the host 
 * by the device as indicated in the following structure declaration
 */
PACKED_TYPESTRUCT(AHCIFis, {
    FISDmaSetup_t    DmaSetup;
    uint8_t          Padding0[4];
    FISPioSetup_t    PioSetup;
    uint8_t          Padding1[12];
    FISRegisterD2H_t RegisterD2H;
    uint8_t          Padding2[4];
    FISDeviceBits_t  DeviceBits;
    uint8_t          UnknownFIS[64];
    uint8_t          ReservedArea[0x100 - 0xA0]; // Offset 0xA0 - Reserved
});

/* Capability Bits (Host Capabilities) 
 * - Generic Registers */
#define AHCI_CAPABILITIES_NP(Caps)          ((Caps & 0x1F) + 1)         // Number of ports
#define AHCI_CAPABILITIES_SXS               0x20                        // Supports External SATA
#define AHCI_CAPABILITIES_EMS               0x40                        // Supports Enclosure Management
#define AHCI_CAPABILITIES_CCCS              0x80                        // Supports Command Completion Coalescing
#define AHCI_CAPABILITIES_NCS(Caps)         (((Caps >> 8) & 0x1F) + 1)  // Number of Command Slots
#define AHCI_CAPABILITIES_PSC               0x2000                      // Partial State Capable
#define AHCI_CAPABILITIES_SSC               0x4000                      // Slumber State Capable
#define AHCI_CAPABILITIES_PMD               0x8000                      // PIO Multiple DRQ Block
#define AHCI_CAPABILITIES_FBSS              0x10000                     // FIS-based Switching Supported
#define AHCI_CAPABILITIES_SPM               0x20000                     // Supports Port Multiplier
#define AHCI_CAPABILITIES_SAM               0x40000                     // Supports AHCI mode only

#define AHCI_CAPABILITIES_ISS(Caps)         ((Caps & 0xF00000) >> 20)   // Interface Speed Support
#define AHCI_SPEED_1_5GBPS                  0x1
#define AHCI_SPEED_3_0GBPS                  0x2
#define AHCI_SPEED_6_0GBPS                  0x3

#define AHCI_CAPABILITIES_SCLO              0x1000000       // Supports Command List Override
#define AHCI_CAPABILITIES_SAL               0x2000000       // Supports Activity LED
#define AHCI_CAPABILITIES_SALP              0x4000000       // Supports Aggressive Link Power Management
#define AHCI_CAPABILITIES_SSS               0x8000000       // Supports Staggered Spin-up
#define AHCI_CAPABILITIES_SMPS              0x10000000      // Supports Mechanical Presence Switch
#define AHCI_CAPABILITIES_SSNTF             0x20000000      // Supports SNotification Register
#define AHCI_CAPABILITIES_SNCQ              0x40000000      // Supports Native Command Queuing
#define AHCI_CAPABILITIES_S64A              0x80000000      // Supports 64-bit Addressing

/* Global HBA Control (GlobalHostControl)
 * - Generic Registers */
#define AHCI_HOSTCONTROL_HR                 0x1             /* HBA Reset */
#define AHCI_HOSTCONTROL_IE                 0x2             /* Global Interrupt Enable */
#define AHCI_HOSTCONTROL_MRSM               0x4             /* MSI Revert to Single Message */
#define AHCI_HOSTCONTROL_AE                 0x80000000      /* AHCI Enable */

/* Port Interrupt Bits (InterruptStatus)
 * - Generic Registers */
#define AHCI_INTERRUPT_PORT(Port)           (1 << Port)

/* Ports Implemented (PortsImplemented)
 * - Generic Registers */
#define AHCI_IMPLEMENTED_PORT(Port)         (1 << Port)

/* AHCI Version (Version)
 * - Generic Registers */
#define AHCI_VERSION_095                    0x00000905
#define AHCI_VERSION_100                    0x00010000
#define AHCI_VERSION_110                    0x00010100
#define AHCI_VERSION_120                    0x00010200
#define AHCI_VERSION_130                    0x00010300
#define AHCI_VERSION_131                    0x00010301

/* Capability Bits Extended (Host Capabilities Extended)
 * - Generic Registers */
#define AHCI_XCAPABILITIES_OS_HANDOFF       0x1             /* BIOS/OS Handoff */
#define AHCI_XCAPABILITIES_NVMP             0x2             /* NVMHCI Present */
#define AHCI_XCAPABILITIES_APST             0x4             /* Automatic Partial to Slumber Transitions */
#define AHCI_XCAPABILITIES_SDS              0x8             /* Supports Device Sleep */
#define AHCI_XCAPABILITIES_SADM             0x10            /* Supports Aggressive Device Sleep Management */
#define AHCI_XCAPABILITIES_DESO             0x20            /* DevSleep Entrance from Slumber Only */

/* BIOS/OS Handoff Control and Status (OSControlAndStatus)
 * - Generic Registers */
#define AHCI_CONTROLSTATUS_BOS              0x1             /* BIOS Owned Semaphore */
#define AHCI_CONTROLSTATUS_OOS              0x2             /* OS Owned Semaphore */
#define AHCI_CONTROLSTATUS_SOOE             0x4             /* SMI on OS Ownership Change Enable */
#define AHCI_CONTROLSTATUS_OOC              0x8             /* OS Ownership Change */
#define AHCI_CONTROLSTATUS_BB               0x10            /* BIOS Busy */

/* Port Control & Status (CommandAndStatus)
 * - Port Registers */
#define AHCI_PORT_ST                        0x1             /* Bit 0: Start */
#define AHCI_PORT_SUD                       0x2             /* Bit 1: Spin-Up Device */
#define AHCI_PORT_POD                       0x4             /* Bit 2: Power On Device */
#define AHCI_PORT_CLO                       0x8             /* Bit 3: Command List Override */
#define AHCI_PORT_FRE                       0x10            /* Bit 4: FIS Receive Enable */
#define AHCI_PORT_CCS(Register)             ((Register >> 8) & 0x1F)    /* Current Command Slot */
#define AHCI_PORT_MPSS                      0x2000          /* Mechanical Presence Switch State */
#define AHCI_PORT_FR                        0x4000          /* FIS Receive Running */
#define AHCI_PORT_CR                        0x8000          /* Command List Running */
#define AHCI_PORT_CPS                       0x10000         /* Cold Presence State */
#define AHCI_PORT_PMA                       0x20000         /* Port Multiplier Attached */
#define AHCI_PORT_HPCP                      0x40000         /* Hot Plug Capable Port */
#define AHCI_PORT_MPSP                      0x80000         /* Mechanical Presence Switch Attached to Port */
#define AHCI_PORT_CPD                       0x100000        /* Cold Presence Detection */
#define AHCI_PORT_ESP                       0x200000        /* External SATA Port */
#define AHCI_PORT_FBSCP                     0x400000        /* FIS-based Switching Capable Port */
#define AHCI_PORT_APSTE                     0x800000        /* Automatic Partial to Slumber Transitions Enabled */
#define AHCI_PORT_ATAPI                     0x1000000       /* Device is ATAPI */
#define AHCI_PORT_DLAE                      0x2000000       /* Drive LED on ATAPI Enable */
#define AHCI_PORT_ALPE                      0x4000000       /* Aggressive Link Power Management Enable */
#define AHCI_PORT_ASP                       0x8000000       /* Aggressive Slumber / Partial */

/* Interface Communication Control */
#define AHCI_PORT_ICC(Register)             ((Register >> 28) & 0xF)
#define AHCI_PORT_ICC_SET(Register, Mode)   Register |= ((Mode & 0xF) << 28)
#define AHCI_PORT_ICC_IDLE                  0x0
#define AHCI_PORT_ICC_ACTIVE                0x1
#define AHCI_PORT_ICC_PARTIAL               0x2
#define AHCI_PORT_ICC_SLUMBER               0x6
#define AHCI_PORT_ICC_DEVSLEEP              0x8

/* Port x Interrupt Enable (InterruptEnable)
 * - Port Registers */
#define AHCI_PORT_IE_DHRE                   0x1             // Device to Host Register FIS Interrupt Enable
#define AHCI_PORT_IE_PSE                    0x2             // PIO Setup FIS Interrupt Enable
#define AHCI_PORT_IE_DSE                    0x4             // DMA Setup FIS Interrupt Enable
#define AHCI_PORT_IE_SDBE                   0x8             // Set Device Bits FIS Interrupt Enable
#define AHCI_PORT_IE_UFE                    0x10            // Unknown FIS Interrupt Enable
#define AHCI_PORT_IE_DPE                    0x20            // Descriptor Processed Interrupt Enable
#define AHCI_PORT_IE_PCE                    0x40            // Port Change Interrupt Enable
#define AHCI_PORT_IE_DMPE                   0x80            // Device Mechanical Presence Enable
#define AHCI_PORT_IE_PRCE                   0x400000        // PhyRdy Change Interrupt Enable
#define AHCI_PORT_IE_IPME                   0x800000        // Incorrect Port Multiplier Enable
#define AHCI_PORT_IE_OFE                    0x1000000       // Overflow Enable
#define AHCI_PORT_IE_INFE                   0x4000000       // Interface Non-fatal Error Enable
#define AHCI_PORT_IE_IFE                    0x8000000       // Interface Fatal Error Enable
#define AHCI_PORT_IE_HBDE                   0x10000000      // Host Bus Data Error Enable
#define AHCI_PORT_IE_HBFE                   0x20000000      // Host Bus Fatal Error Enable
#define AHCI_PORT_IE_TFEE                   0x40000000      // Task File Error Enable
#define AHCI_PORT_IE_CPDE                   0x80000000      // Cold Presence Detect Enable

/* Port x Task File Data (TaskFileData)
 * - Port Registers */
#define AHCI_PORT_TFD_ERR                   0x1
#define AHCI_PORT_TFD_DRQ                   0x8
#define AHCI_PORT_TFD_RDY                   0x40
#define AHCI_PORT_TFD_BSY                   0x80

/* Port Ata Control (AtaControl)
 * - Port Registers */
#define AHCI_PORT_SCTL_RESET                 0x1
#define AHCI_PORT_SCTL_DISABLE               0x4

#define AHCI_PORT_SCTL_SPEED_LIMIT_GEN1      0x10
#define AHCI_PORT_SCTL_SPEED_LIMIT_GEN2      0x20
#define AHCI_PORT_SCTL_SPEED_LIMIT_GEN3      0x30

#define AHCI_PORT_SCTL_DISABLE_PARTIAL_STATE 0x100
#define AHCI_PORT_SCTL_DISABLE_SLUMBER_STATE 0x200
#define AHCI_PORT_SCTL_DISABLE_DEVSLEEP_PWM  0x400

/* Port Ata Status (AtaStatus)
 * - Port Registers */
#define AHCI_PORT_STSS_DET(Sts)             (Sts & 0xF)
#define AHCI_PORT_SSTS_DET_NODEVICE         0x0
#define AHCI_PORT_SSTS_DET_NOPHYCOM         0x1
#define AHCI_PORT_SSTS_DET_ENABLED          0x3
#define AHCI_PORT_SSTS_DET_DISABLED         0x4

// PORT SERR
#define AHCI_PORT_SERR_ERROR(reg) (reg & 0xFFFF)
#define AHCI_PORT_SERR_DIAG(reg)  ((reg >> 16) & 0xFFFF)

typedef struct AhciPort {
    int                     Id;
    int                     Index;
    int                     MultiplierIndex;
    int                     Connected;
    AHCIPortRegisters_t*    Registers;
    
    SHMHandle_t         InternalBuffer;
    SHMHandle_t         CommandListDMA;
    SHMHandle_t         CommandTableDMA;
    SHMHandle_t         RecievedFisDMA;

    _Atomic(int)            Slots;
    int                     SlotCount;
    list_t                  Transactions;
} AhciPort_t;

/* AhciInterruptResource
 * The shared interrupt resource that is used to store data from the fast interrupt
 * into process interrupt. */
typedef struct AhciInterruptResource {
    _Atomic(reg32_t) ControllerInterruptStatus;
    _Atomic(reg32_t) PortInterruptStatus[AHCI_MAX_PORTS];
} AhciInterruptResource_t;

typedef struct AhciController {
    BusDevice_t*            Device;
    element_t               header;
    AhciInterruptResource_t InterruptResource;
    uuid_t                  InterruptId;
    spinlock_t              Lock;
    int                     event_descriptor;

    DeviceIo_t*             IoBase;
    AHCIGenericRegisters_t* Registers;

    AhciPort_t*             Ports[AHCI_MAX_PORTS];
    uint32_t                ValidPorts;
    int                     PortCount;
    int                     CommandSlotCount;
} AhciController_t;

/* AhciControllerCreate
 * Registers a new controller with the AHCI driver */
__EXTERN AhciController_t*
AhciControllerCreate(
    _In_ BusDevice_t* busDevice);

/* AhciControllerDestroy
 * Destroys an existing controller instance and cleans up
 * any resources related to it */
__EXTERN oserr_t
AhciControllerDestroy(
    _In_ AhciController_t*  controller);

/* AhciPortCreate
 * Initializes the port structure, but not memory structures yet */
__EXTERN AhciPort_t*
AhciPortCreate(
    _In_ AhciController_t*  controller,
    _In_ int                portIndex,
    _In_ int                mapIndex);

/* AhciPortCleanup
 * Destroys a port, cleans up device, cleans up memory and resources */
__EXTERN void
AhciPortCleanup(
    _In_ AhciController_t*  Controller, 
    _In_ AhciPort_t*        Port);

/* AhciPortInitiateSetup
 * Initiates the setup sequence, this function needs at-least 500ms to complete before calling finish setup. */
__EXTERN void
AhciPortInitiateSetup(
    _In_ AhciController_t*  controller,
    _In_ AhciPort_t*        port);

/* AhciPortFinishSetup
 * Finishes setup of port by completing a reset sequence. */
__EXTERN oserr_t
AhciPortFinishSetup(
    _In_ AhciController_t*  controller,
    _In_ AhciPort_t*        port);

/* AhciPortRebase
 * Rebases the port by setting up allocated memory tables and command memory. This can only be done
 * when the port is in a disabled state. */
__EXTERN oserr_t
AhciPortRebase(
    _In_ AhciController_t*  controller,
    _In_ AhciPort_t*        port);

/* AhciPortStart
 * Starts the port, the port must have been in a disabled state and must have been rebased at-least once. */
__EXTERN oserr_t
AhciPortStart(
    _In_ AhciController_t*  controller,
    _In_ AhciPort_t*        port);

oserr_t
AhciPortAllocateCommandSlot(
    _In_  AhciPort_t* port,
    _Out_ int*        slotOut);

void
AhciPortFreeCommandSlot(
    _In_ AhciPort_t* port,
    _In_ int         slot);

/* AhciPortStartCommandSlot
 * Starts a command slot on the given port */
__EXTERN void
AhciPortStartCommandSlot(
    _In_ AhciPort_t*        port,
    _In_ int                slot,
    _In_ int                nqc);

/* AhciPortInterruptHandler
 * Handles port-specific interrupts. */
__EXTERN void
AhciPortInterruptHandler(
    _In_ AhciController_t*  controller,
    _In_ AhciPort_t*        port);

/* AhciDeviceIdentify 
 * Identifies the device and type on a port and sets it up accordingly */
__EXTERN void
AhciDeviceIdentify(
    _In_ AhciController_t*  Controller, 
    _In_ AhciPort_t*        Port);

/* PrintTaskDataErrorString
 * Converts the error of the task data to a user-readable string and prints it out. */
__EXTERN void
PrintTaskDataErrorString(uint8_t taskDataError);

#endif //!_AHCI_H_
