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
* MollenOS X86-32 USB UHCI Controller Driver
* Todo:
* Fix the interrupt spam of HcHalted
* Figure out how we can send transactions correctly
* For gods sake make it work, and get some sleep
*/

/* Includes */
#include <Module.h>
#include "Uhci.h"

#include <Scheduler.h>
#include <UsbCore.h>
#include <Timers.h>
#include <Heap.h>
#include <Pci.h>

/* CLib */
#include <string.h>

/* Globals */
uint32_t GlbUhciId = 0;

const char *GlbUhciDriverName = "MollenOS UHCI Driver";
const char *UhciErrorMessages[] =
{
	"No Error",
	"Bitstuff Error",
	"CRC/Timeout Error",
	"NAK Recieved",
	"Babble Detected",
	"Data Buffer Error",
	"Stalled",
	"Active"
};

//#define UHCI_DIAGNOSTICS

/* Prototypes (Internal) */
void UhciInitQueues(UhciController_t *Controller);
void UhciSetup(UhciController_t *Controller);
int UhciInterruptHandler(void *Args);

/* Port Callbacks */
void UhciPortSetup(void *Data, UsbHcPort_t *Port);
void UhciPortsCheck(void *Data);

/* Ep Callbacks */
void UhciEndpointSetup(void *Controller, UsbHcEndpoint_t *Endpoint);
void UhciEndpointDestroy(void *Controller, UsbHcEndpoint_t *Endpoint);

/* Transaction Prototypes */
void UhciTransactionInit(void *Controller, UsbHcRequest_t *Request);
UsbHcTransaction_t *UhciTransactionSetup(void *Controller, UsbHcRequest_t *Request);
UsbHcTransaction_t *UhciTransactionIn(void *Controller, UsbHcRequest_t *Request);
UsbHcTransaction_t *UhciTransactionOut(void *Controller, UsbHcRequest_t *Request);
void UhciTransactionSend(void *Controller, UsbHcRequest_t *Request);
void UhciTransactionDestroy(void *Controller, UsbHcRequest_t *Request);

/* Helpers */
uint16_t UhciRead16(UhciController_t *Controller, uint16_t Register)
{
	/* Read */
	uint16_t Value = (uint16_t)IoSpaceRead(Controller->IoBase, Register, 2);

	/* Done! */
	return Value;
}

uint32_t UhciRead32(UhciController_t *Controller, uint16_t Register)
{
	/* Read */
	uint32_t Value = (uint32_t)IoSpaceRead(Controller->IoBase, Register, 4);

	/* Done! */
	return Value;
}

void UhciWrite8(UhciController_t *Controller, uint16_t Register, uint8_t Value)
{
	/* Write new state */
	IoSpaceWrite(Controller->IoBase, Register, Value, 1);
}

void UhciWrite16(UhciController_t *Controller, uint16_t Register, uint16_t Value)
{ 
	/* Write new state */
	IoSpaceWrite(Controller->IoBase, Register, Value, 2);
}

void UhciWrite32(UhciController_t *Controller, uint16_t Register, uint32_t Value)
{
	/* Write new state */
	IoSpaceWrite(Controller->IoBase, Register, Value, 4);
}

/* Entry point of a module */
MODULES_API void ModuleInit(void *Data)
{
	/* Vars */
	MCoreDevice_t *mDevice = (MCoreDevice_t*)Data;
	UhciController_t *Controller = NULL;
	uint16_t PciCommand;

	/* Allocate Resources for this controller */
	Controller = (UhciController_t*)kmalloc(sizeof(UhciController_t));
	memset(Controller, 0, sizeof(UhciController_t));
	Controller->Id = GlbUhciId;
	Controller->Device = mDevice;

	/* Setup Lock */
	SpinlockReset(&Controller->Lock);
	GlbUhciId++;

	/* Get I/O Base */
	for (PciCommand = 0; PciCommand < DEVICEMANAGER_MAX_IOSPACES; PciCommand++) {
		if (mDevice->IoSpaces[PciCommand] != NULL
			&& mDevice->IoSpaces[PciCommand]->Type == DEVICE_IO_SPACE_IO) {
			Controller->IoBase = mDevice->IoSpaces[PciCommand];
			break;
		}
	}
	
	/* Sanity */
	if (Controller->IoBase == NULL)
	{
		/* Yea, give me my hat back */
		LogFatal("UHCI", "Invalid memory space!");
		kfree(Controller);
		return;
	}

	/* Get DMA */
	Controller->FrameListPhys = 
		(Addr_t)AddressSpaceMap(AddressSpaceGetCurrent(), 
			0, PAGE_SIZE, ADDRESS_SPACE_FLAG_LOWMEM);
	Controller->FrameList = 
		(void*)Controller->FrameListPhys;

	/* Memset */
	memset(Controller->FrameList, 0, PAGE_SIZE);

	/* Allocate Irq */
	mDevice->IrqAvailable[0] = -1;
	mDevice->IrqHandler = UhciInterruptHandler;

	/* Register us for an irq */
	if (DmRequestResource(mDevice, ResourceIrq)) {

		/* Damnit! */
		LogFatal("UHCI", "Failed to allocate irq for use, bailing out!");
		kfree(Controller);
		return;
	}

	/* Enable i/o and Bus mastering and clear interrupt disable */
	PciCommand = (uint16_t)PciDeviceRead(mDevice->BusDevice, 0x4, 2);
	PciDeviceWrite(mDevice->BusDevice, 0x4, (PciCommand & ~(0x400)) | 0x1 | 0x4, 2);

	/* Setup driver information */
	mDevice->Driver.Name = (char*)GlbUhciDriverName;
	mDevice->Driver.Data = Controller;
	mDevice->Driver.Version = 1;
	mDevice->Driver.Status = DriverActive;

	/* Reset Controller */
	UhciSetup(Controller);
}

/* Aligns address (with roundup if alignment is set) */
Addr_t UhciAlign(Addr_t BaseAddr, Addr_t AlignmentBits, Addr_t Alignment)
{
	/* Save, so we can modify */
	Addr_t AlignedAddr = BaseAddr;

	/* Only align if unaligned */
	if (AlignedAddr & AlignmentBits)
	{
		AlignedAddr &= ~AlignmentBits;
		AlignedAddr += Alignment;
	}

	/* Done */
	return AlignedAddr;
}

/* The two functions below are to setup QH Frame List */
uint32_t UhciFFS(uint32_t Value)
{
	/* Return Value */
	uint32_t RetNum = 0;

	/* 16 Bits */
	if (!(Value & 0xFFFF))
	{
		RetNum += 16;
		Value >>= 16;
	}

	/* 8 Bits */
	if (!(Value & 0xFF))
	{
		RetNum += 8;
		Value >>= 8;
	}

	/* 4 Bits */
	if (!(Value & 0xF))
	{
		RetNum += 4;
		Value >>= 4;
	}

	/* 2 Bits */
	if (!(Value & 0x3))
	{
		RetNum += 2;
		Value >>= 2;
	}

	/* 1 Bit */
	if (!(Value & 0x1))
		RetNum++;

	/* Done */
	return RetNum;
}

/* Determine Qh for Interrupt Transfer */
uint32_t UhciDetermineInterruptQh(UhciController_t *Controller, uint32_t Frame)
{
	/* Resulting Index */
	uint32_t Index;

	/* Determine index from first free bit 
	 * 8 queues */
	Index = 8 - UhciFFS(Frame | UHCI_NUM_FRAMES);

	/* Sanity */
	if (Index < 2 || Index > 8)
		Index = UHCI_POOL_ASYNC;

	/* Return Phys */
	return (Controller->QhPoolPhys[Index] | UHCI_TD_LINK_QH);
}

/* Start / Stop */
void UhciStart(UhciController_t *Controller)
{
	/* Send run command */
	uint16_t OldCmd = UhciRead16(Controller, UHCI_REGISTER_COMMAND);
	OldCmd |= (UHCI_CMD_CONFIGFLAG | UHCI_CMD_RUN | UHCI_CMD_MAXPACKET64);
	UhciWrite16(Controller, UHCI_REGISTER_COMMAND, OldCmd);

	/* Wait for it to start */	
	OldCmd = 0;
	WaitForConditionWithFault(OldCmd, 
		(UhciRead16(Controller, UHCI_REGISTER_STATUS) & UHCI_STATUS_HALTED) == 0, 100, 10);
}

void UhciStop(UhciController_t *Controller)
{
	/* Send stop command */
	uint16_t OldCmd = UhciRead16(Controller, UHCI_REGISTER_COMMAND);
	OldCmd &= ~(UHCI_CMD_RUN);
	UhciWrite16(Controller, UHCI_REGISTER_COMMAND, OldCmd);
}

/* Read current frame number */
void UhciGetCurrentFrame(UhciController_t *Controller)
{
	/* Vars */
	uint16_t FrameNo = 0, Diff = 0;

	/* Read */
	FrameNo = UhciRead16(Controller, UHCI_REGISTER_FRNUM);

	/* Calc diff */
	Diff = (FrameNo - Controller->Frame) & (UHCI_FRAME_MASK - 1);

	/* Add */
	Controller->Frame += Diff;
}

/* Convert condition code to nr */
int UhciConditionCodeToIndex(int ConditionCode)
{
	/* Vars */
	int bCount = 0;
	int Cc = ConditionCode;

	/* Keep bit-shifting */
	for (; Cc != 0;) {
		bCount++;
		Cc >>= 1;
	}

	/* Done */
	return bCount;
}

/* Resets the Controller */
void UhciReset(UhciController_t *Controller)
{
	/* Vars */
	uint16_t Temp = 0;

	/* Write HCReset */
	UhciWrite16(Controller, UHCI_REGISTER_COMMAND, UHCI_CMD_HCRESET);

	/* Wait */
	WaitForConditionWithFault(Temp, (UhciRead16(Controller, UHCI_REGISTER_COMMAND) & UHCI_CMD_HCRESET) == 0, 100, 10);

	/* Sanity */
	if (Temp == 1)
		LogDebug("UHCI", "Reset signal is still active..");

	/* Clear out to be safe */
	UhciWrite16(Controller, UHCI_REGISTER_COMMAND, 0x0000);
	UhciWrite16(Controller, UHCI_REGISTER_INTR, 0x0000);

	/* Now re-configure it */
	UhciWrite8(Controller, UHCI_REGISTER_SOFMOD, 64); /* Frame Length 1 ms */
	UhciWrite32(Controller, UHCI_REGISTER_FRBASEADDR, Controller->FrameListPhys);
	UhciWrite16(Controller, UHCI_REGISTER_FRNUM, (Controller->Frame & UHCI_FRAME_MASK));

	/* Enable interrupts */
	UhciWrite16(Controller, UHCI_REGISTER_INTR,
		(UHCI_INTR_TIMEOUT | UHCI_INTR_SHORT_PACKET
		| UHCI_INTR_RESUME | UHCI_INTR_COMPLETION));

	/* Start Controller */
	UhciStart(Controller);
}

/* Initializes the Controller */
void UhciSetup(UhciController_t *Controller)
{
	/* Vars */
	UsbHc_t *Hcd;
	uint16_t Temp = 0, i = 0;

	/* Disable interrupts while configuring (and stop controller) */
	UhciWrite16(Controller, UHCI_REGISTER_COMMAND, 0x0000);
	UhciWrite16(Controller, UHCI_REGISTER_INTR, 0x0000);

	/* Global Reset */
	UhciWrite16(Controller, UHCI_REGISTER_COMMAND, UHCI_CMD_GRESET);
	StallMs(100);
	UhciWrite16(Controller, UHCI_REGISTER_COMMAND, 0x0000);

	/* Disable stuff again, we don't know what state is set after reset */
	UhciWrite16(Controller, UHCI_REGISTER_COMMAND, 0x0000);
	UhciWrite16(Controller, UHCI_REGISTER_INTR, 0x0000);

	/* Setup Queues */
	UhciInitQueues(Controller);

	/* Reset */
	UhciWrite16(Controller, UHCI_REGISTER_COMMAND, UHCI_CMD_HCRESET);

	/* Wait */
	WaitForConditionWithFault(Temp, (UhciRead16(Controller, UHCI_REGISTER_COMMAND) & UHCI_CMD_HCRESET) == 0, 100, 10);

	/* Sanity */
	if (Temp == 1)
		LogDebug("UHCI", "Reset signal is still active..");

	/* Clear out to be safe */
	UhciWrite16(Controller, UHCI_REGISTER_COMMAND, 0x0000);
	UhciWrite16(Controller, UHCI_REGISTER_INTR, 0x0000);

	/* Now re-configure it */
	UhciWrite8(Controller, UHCI_REGISTER_SOFMOD, 64); /* Frame Length 1 ms */
	UhciWrite32(Controller, UHCI_REGISTER_FRBASEADDR, Controller->FrameListPhys);
	UhciWrite16(Controller, UHCI_REGISTER_FRNUM, (Controller->Frame & UHCI_FRAME_MASK));

	/* We get port count & 0 them */
	for (i = 0; i <= UHCI_MAX_PORTS; i++)
	{
		Temp = UhciRead16(Controller, (UHCI_REGISTER_PORT_BASE + (i * 2)));

		/* Is it a valid port? */
		if (!(Temp & UHCI_PORT_RESERVED)
			|| Temp == 0xFFFF)
		{
			/* This reserved bit must be 1 */
			/* And we must have 2 ports atleast */
			break;
		}
	}

	/* Ports are now i */
	Controller->NumPorts = i;

	/* Enable PCI Interrupts */
	PciDeviceWrite(Controller->Device->BusDevice, UHCI_USBLEGEACY, 0x2000, 2);

	/* If vendor is Intel we null out the intel register */
	if (Controller->Device->VendorId == 0x8086)
		PciDeviceWrite(Controller->Device->BusDevice, UHCI_USBRES_INTEL, 0x00, 1);

	/* Enable interrupts */
	UhciWrite16(Controller, UHCI_REGISTER_INTR,
		(UHCI_INTR_TIMEOUT | UHCI_INTR_SHORT_PACKET
		| UHCI_INTR_RESUME | UHCI_INTR_COMPLETION));

	/* Start Controller */
	UhciStart(Controller);

	/* Setup HCD */
	Hcd = UsbInitController((void*)Controller, UhciController, Controller->NumPorts);

	/* Setup functions */
	Hcd->RootHubCheck = UhciPortsCheck;
	Hcd->PortSetup = UhciPortSetup;
	Hcd->Reset = UhciReset;

	/* Ep Functions */
	Hcd->EndpointSetup = UhciEndpointSetup;
	Hcd->EndpointDestroy = UhciEndpointDestroy;

	/* Transaction Functions */
	Hcd->TransactionInit = UhciTransactionInit;
	Hcd->TransactionSetup = UhciTransactionSetup;
	Hcd->TransactionIn = UhciTransactionIn;
	Hcd->TransactionOut = UhciTransactionOut;
	Hcd->TransactionSend = UhciTransactionSend;
	Hcd->TransactionDestroy = UhciTransactionDestroy;

	/* Register it */
	Controller->HcdId = UsbRegisterController(Hcd);

	/* Install Periodic Check (NO HUB INTERRUPTS!?)
	 * Anyway this will initiate ports */
	TimersCreateTimer(UhciPortsCheck, Controller, TimerPeriodic, 1000);
}

/* Initialises Queue Heads & Interrupt Queeue */
void UhciInitQueues(UhciController_t *Controller)
{
	/* Setup Vars */
	uint32_t *FrameListPtr = (uint32_t*)Controller->FrameList;
	Addr_t Pool = 0, PoolPhysical = 0;
	uint32_t i;

	/* Setup Null Td */
	Pool = (Addr_t)kmalloc(sizeof(UhciTransferDescriptor_t) + UHCI_STRUCT_ALIGN);
	Controller->NullTd = (UhciTransferDescriptor_t*)UhciAlign(Pool, UHCI_STRUCT_ALIGN_BITS, UHCI_STRUCT_ALIGN);
	Controller->NullTdPhysical = 
		AddressSpaceGetMap(AddressSpaceGetCurrent(), (VirtAddr_t)Controller->NullTd);

	/* Memset it */
	memset((void*)Controller->NullTd, 0, sizeof(UhciTransferDescriptor_t));

	/* Set link invalid */
	Controller->NullTd->Header = (uint32_t)(UHCI_TD_PID_IN | UHCI_TD_DEVICE_ADDR(0x7F) | UHCI_TD_MAX_LEN(0x7FF));
	Controller->NullTd->Link = UHCI_TD_LINK_END;

	/* Setup Qh Pool */
	Pool = (Addr_t)kmalloc((sizeof(UhciQueueHead_t) * UHCI_POOL_NUM_QH) + UHCI_STRUCT_ALIGN);
	Pool = UhciAlign(Pool, UHCI_STRUCT_ALIGN_BITS, UHCI_STRUCT_ALIGN);
	PoolPhysical = (Addr_t)AddressSpaceGetMap(AddressSpaceGetCurrent(), Pool);

	/* Null out pool */
	memset((void*)Pool, 0, sizeof(UhciQueueHead_t) * UHCI_POOL_NUM_QH);

	/* Set them up */
	for (i = 0; i < UHCI_POOL_NUM_QH; i++)
	{
		/* Set QH */
		Controller->QhPool[i] = (UhciQueueHead_t*)Pool;
		Controller->QhPoolPhys[i] = PoolPhysical;

		/* Set its index */
		Controller->QhPool[i]->Flags = UHCI_QH_INDEX(i);

		/* Increase */
		Pool += sizeof(UhciQueueHead_t);
		PoolPhysical += sizeof(UhciQueueHead_t);
	}

	/* Setup interrupt queues */
	for (i = UHCI_POOL_ISOCHRONOUS + 1; i < UHCI_POOL_ASYNC; i++)
	{
		/* Set QH Link */
		Controller->QhPool[i]->Link = (Controller->QhPoolPhys[UHCI_POOL_ASYNC] | UHCI_TD_LINK_QH);
		Controller->QhPool[i]->LinkVirtual = (uint32_t)Controller->QhPool[UHCI_POOL_ASYNC];

		/* Disable TD List */
		Controller->QhPool[i]->Child = UHCI_TD_LINK_END;
		Controller->QhPool[i]->ChildVirtual = 0;

		/* Set in use */
		Controller->QhPool[i]->Flags |= (UHCI_QH_SET_QUEUE(i) | UHCI_QH_ACTIVE);
	}
	
	/* Setup Iso Qh */

	/* Setup null QH */
	Controller->QhPool[UHCI_POOL_FSBR]->Link =
		(Controller->QhPoolPhys[UHCI_POOL_FSBR] | UHCI_TD_LINK_QH);
	Controller->QhPool[UHCI_POOL_FSBR]->LinkVirtual =
		(uint32_t)Controller->QhPool[UHCI_POOL_FSBR];
	Controller->QhPool[UHCI_POOL_FSBR]->Child = Controller->NullTdPhysical;
	Controller->QhPool[UHCI_POOL_FSBR]->ChildVirtual = (uint32_t)Controller->NullTd;
	Controller->QhPool[UHCI_POOL_FSBR]->Flags |= UHCI_QH_ACTIVE;

	/* Setup async Qh */
	Controller->QhPool[UHCI_POOL_ASYNC]->Link = UHCI_TD_LINK_END;
	Controller->QhPool[UHCI_POOL_ASYNC]->LinkVirtual = 0;
	Controller->QhPool[UHCI_POOL_ASYNC]->Child = Controller->NullTdPhysical;
	Controller->QhPool[UHCI_POOL_ASYNC]->ChildVirtual = (uint32_t)Controller->NullTd;
	Controller->QhPool[UHCI_POOL_ASYNC]->Flags |= UHCI_QH_ACTIVE;

	/* Set all queues to end in the async QH 
	 * This way we handle iso & ints before bulk/control */
	for (i = UHCI_POOL_ISOCHRONOUS + 1; i < UHCI_POOL_ASYNC; i++) {
		Controller->QhPool[i]->Link = 
			Controller->QhPoolPhys[UHCI_POOL_ASYNC] | UHCI_TD_LINK_QH;
		Controller->QhPool[i]->LinkVirtual = 
			(uint32_t)Controller->QhPoolPhys[UHCI_POOL_ASYNC];
	}

	/* 1024 Entries
	* Set all entries to the 8 interrupt queues, and we
	* want them interleaved such that some queues get visited more than others */
	for (i = 0; i < UHCI_NUM_FRAMES; i++)
		FrameListPtr[i] = UhciDetermineInterruptQh(Controller, i);

	/* Init transaction list */
	Controller->TransactionList = list_create(LIST_SAFE);
}

/* Ports */
void UhciPortReset(UhciController_t *Controller, uint32_t Port)
{
	/* Calc */
	uint16_t Temp;
	uint16_t pOffset = (UHCI_REGISTER_PORT_BASE + ((uint16_t)Port * 2));

	/* Step 1. Send reset signal */
	UhciWrite16(Controller, pOffset, UHCI_PORT_RESET);

	/* Wait atlest 50 ms (per USB specification) */
	StallMs(60);

	/* Now deassert reset signal */
	Temp = UhciRead16(Controller, pOffset);
	UhciWrite16(Controller, pOffset, Temp & ~UHCI_PORT_RESET);

	/* Wait for CSC */
	Temp = 0;
	WaitForConditionWithFault(Temp,
		(UhciRead16(Controller, pOffset) & UHCI_PORT_CONNECT_STATUS), 10, 1);

	/* Even if it fails, try to enable anyway */
	if (Temp) {
		LogDebug("UHCI", "Port failed to come online in a timely manner after reset...");
	}

	/* Step 2. Enable Port & clear event bits */
	Temp = UhciRead16(Controller, pOffset);
	UhciWrite16(Controller, pOffset, 
		Temp | UHCI_PORT_CONNECT_EVENT | UHCI_PORT_ENABLED_EVENT | UHCI_PORT_ENABLED);

	/* Wait for enable, with timeout */
	Temp = 0;
	WaitForConditionWithFault(Temp,
		(UhciRead16(Controller, pOffset) & UHCI_PORT_ENABLED), 25, 10);

	/* Sanity */
	if (Temp) {
		LogDebug("UHCI", "Port %u enable time-out!", Port);
	}

	/* Wait 30 ms more 
	 * I found this wait to be EXTREMELY 
	 * crucical, otherwise devices would stall. 
	 * because I accessed them to quickly after the reset */
	StallMs(30);
}

/* Detect any port changes */
void UhciPortCheck(UhciController_t *Controller, int Port)
{
	/* Get port status */
	uint16_t pStatus = UhciRead16(Controller, (UHCI_REGISTER_PORT_BASE + ((uint16_t)Port * 2)));
	UsbHc_t *Hcd;

	/* Has there been a connection event? */
	if (!(pStatus & UHCI_PORT_CONNECT_EVENT))
		return;

	/* Clear connection event */
	UhciWrite16(Controller, 
		(UHCI_REGISTER_PORT_BASE + ((uint16_t)Port * 2)), UHCI_PORT_CONNECT_EVENT);

	/* Get HCD data */
	Hcd = UsbGetHcd(Controller->HcdId);

	/* Sanity */
	if (Hcd == NULL)
		return;

	/* Connect event? */
	if (pStatus & UHCI_PORT_CONNECT_STATUS)
	{
		/* Connection Event */
		UsbEventCreate(Hcd, Port, HcdConnectedEvent);
	}
	else
	{
		/* Disconnect Event */
		UsbEventCreate(Hcd, Port, HcdDisconnectedEvent);
	}
}

/* Go through ports */
void UhciPortsCheck(void *Data)
{
	/* Cast & Vars */
	UhciController_t *Controller = (UhciController_t*)Data;
	int i;

	/* Iterate ports and check */
	for (i = 0; i < (int)Controller->NumPorts; i++)
		UhciPortCheck(Controller, i);
}

/* Gets port status */
void UhciPortSetup(void *Data, UsbHcPort_t *Port)
{
	UhciController_t *Controller = (UhciController_t*)Data;
	uint16_t pStatus = 0;

	/* Reset Port */
	UhciPortReset(Controller, Port->Id);

	/* Dump info */
	pStatus = UhciRead16(Controller, (UHCI_REGISTER_PORT_BASE + ((uint16_t)Port->Id * 2)));

#ifdef UHCI_DIAGNOSTICS
	LogDebug("UHCI", "UHCI %u.%u Status: 0x%x", Controller->Id, Port->Id, pStatus);
#endif

	/* Is it connected? */
	if (pStatus & UHCI_PORT_CONNECT_STATUS)
		Port->Connected = 1;
	else
		Port->Connected = 0;

	/* Enabled? */
	if (pStatus & UHCI_PORT_ENABLED)
		Port->Enabled = 1;
	else
		Port->Enabled = 0;

	/* Lowspeed? */
	if (pStatus & UHCI_PORT_LOWSPEED)
		Port->Speed = LowSpeed;
	else
		Port->Speed = FullSpeed;
}

/* QH Functions */
UhciQueueHead_t *UhciAllocateQh(UhciController_t *Controller, UsbTransferType_t Type)
{
	/* Vars */
	UhciQueueHead_t *Qh = NULL;
	int i;

	/* Pick a QH */
	SpinlockAcquire(&Controller->Lock);

	/* Grap it, locked operation */
	if (Type == ControlTransfer
		|| Type == BulkTransfer)
	{
		/* Grap Index */
		for (i = UHCI_POOL_START; i < UHCI_POOL_NUM_QH; i++)
		{
			/* Sanity */
			if (Controller->QhPool[i]->Flags & UHCI_QH_ACTIVE)
				continue;

			/* First thing to do is setup flags */
			Controller->QhPool[i]->Flags = 
				UHCI_QH_ACTIVE | UHCI_QH_INDEX(i) | UHCI_QH_SET_QUEUE(UHCI_POOL_ASYNC)
				| UHCI_QH_TYPE((uint32_t)Type) | UHCI_QH_BANDWIDTH_ALLOC;

			/* Set index */
			Qh = Controller->QhPool[i];
			break;
		}

		/* Sanity */
		if (i == UHCI_POOL_NUM_QH)
			kernel_panic("USB_UHCI::WTF RAN OUT OF QH's\n");
	}
	else
	{
		/* Iso & Int */

		/* Allocate */
		Addr_t aSpace = (Addr_t)kmalloc(sizeof(UhciQueueHead_t) + UHCI_STRUCT_ALIGN);
		Qh = (UhciQueueHead_t*)UhciAlign(aSpace, UHCI_STRUCT_ALIGN_BITS, UHCI_STRUCT_ALIGN);

		/* Zero it out */
		memset((void*)Qh, 0, sizeof(UhciQueueHead_t));

		/* Setup flags */
		Qh->Flags =
			UHCI_QH_ACTIVE | UHCI_QH_INDEX(0xFF) | UHCI_QH_TYPE((uint32_t)Type);
	}

	/* Release lock */
	SpinlockRelease(&Controller->Lock);

	/* Done */
	return Qh;
}

/* Initializes the Qh */
void UhciQhInit(UhciController_t *Controller, UhciQueueHead_t *Qh, UsbHcTransaction_t *FirstTd)
{
	/* Set link pointer */
	Qh->Link = Controller->QhPoolPhys[UHCI_POOL_FSBR] | UHCI_TD_LINK_QH;
	Qh->LinkVirtual = (uint32_t)Controller->QhPool[UHCI_POOL_FSBR];

	/* Set Td list */
	if ((Addr_t)FirstTd->TransferDescriptor == UHCI_TD_LINK_END)
	{
		Qh->Child = UHCI_TD_LINK_END;
		Qh->ChildVirtual = 0;
	}
	else
	{
		Qh->Child = AddressSpaceGetMap(AddressSpaceGetCurrent(), (Addr_t)FirstTd->TransferDescriptor);
		Qh->ChildVirtual = (uint32_t)FirstTd->TransferDescriptor;
	}
}

/* TD Functions */
Addr_t OhciAllocateTd(UhciEndpoint_t *Ep, UsbTransferType_t Type)
{
	/* Vars */
	size_t i;
	Addr_t cIndex = 0;
	UhciTransferDescriptor_t *Td;

	/* Pick a QH */
	SpinlockAcquire(&Ep->Lock);

	/* Sanity */
	if (Type == ControlTransfer
		|| Type == BulkTransfer)
	{
		/* Grap it, locked operation */
		for (i = 0; i < Ep->TdsAllocated; i++)
		{
			/* Sanity */
			if (Ep->TDPool[i]->HcdFlags & UHCI_TD_HCD_ALLOCATED)
				continue;

			/* Found one, set flags  */
			Ep->TDPool[i]->HcdFlags = UHCI_TD_HCD_ALLOCATED;

			/* Return index */
			cIndex = (Addr_t)i;
			break;
		}

		/* Sanity */
		if (i == Ep->TdsAllocated)
			kernel_panic("USB_UHCI::WTF ran out of TD's!!!!\n");
	}
	else
	{
		/* Isochronous & Interrupt */

		/* Allocate a new */
		Td = (UhciTransferDescriptor_t*)UhciAlign((
			(Addr_t)kmalloc(sizeof(UhciTransferDescriptor_t) + UHCI_STRUCT_ALIGN)), 
				UHCI_STRUCT_ALIGN_BITS, UHCI_STRUCT_ALIGN);

		/* Null it */
		memset((void*)Td, 0, sizeof(UhciTransferDescriptor_t));

		/* Found one, set flags  */
		Td->HcdFlags = UHCI_TD_HCD_ALLOCATED;

		/* Set as index */
		cIndex = (Addr_t)Td;
	}

	/* Release Lock */
	SpinlockRelease(&Ep->Lock);

	return cIndex;
}

/* Setup TD */
UhciTransferDescriptor_t *UhciTdSetup(UhciEndpoint_t *Ep, UsbTransferType_t Type,
	UsbPacket_t *pPacket, size_t DeviceAddr,
	size_t EpAddr, UsbSpeed_t Speed, void **TDBuffer)
{
	/* Vars */
	UhciTransferDescriptor_t *Td;
	void *Buffer;
	Addr_t TDIndex;

	/* Allocate a Td */
	TDIndex = OhciAllocateTd(Ep, Type);

	/* Grab a Td and a Buffer */
	Td = Ep->TDPool[TDIndex];
	Buffer = Ep->TDPoolBuffers[TDIndex];

	/* Set Link */
	Td->Link = UHCI_TD_LINK_END;

	/* Setup TD Control Status */
	Td->Flags = UHCI_TD_ACTIVE;
	Td->Flags |= UHCI_TD_SET_ERR_CNT(3);

	if (Speed == LowSpeed)
		Td->Flags |= UHCI_TD_LOWSPEED;

	/* Setup TD Header Packet */
	Td->Header = UHCI_TD_PID_SETUP;
	Td->Header |= UHCI_TD_DEVICE_ADDR(DeviceAddr);
	Td->Header |= UHCI_TD_EP_ADDR(EpAddr);
	Td->Header |= UHCI_TD_MAX_LEN((sizeof(UsbPacket_t) - 1));

	/* Setup SETUP packet */
	*TDBuffer = Buffer;
	memcpy(Buffer, (void*)pPacket, sizeof(UsbPacket_t));

	/* Set buffer */
	Td->Buffer = AddressSpaceGetMap(AddressSpaceGetCurrent(), (VirtAddr_t)Buffer);

	/* Done */
	return Td;
}

/* In/Out TD */
UhciTransferDescriptor_t *UhciTdIo(UhciEndpoint_t *Ep, UsbTransferType_t Type,
	size_t Toggle, size_t DeviceAddr, size_t EpAddr,
	uint32_t PId, size_t Length, UsbSpeed_t Speed, void **TDBuffer)
{
	/* Vars */
	UhciTransferDescriptor_t *Td;
	void *Buffer;
	Addr_t TDIndex;

	/* Allocate a Td */
	TDIndex = OhciAllocateTd(Ep, Type);

	/* Grab a buffer */
	if (Type == ControlTransfer || Type == BulkTransfer) {
		Td = Ep->TDPool[TDIndex];
		Buffer = Ep->TDPoolBuffers[TDIndex];
	}
	else {
		Td = (UhciTransferDescriptor_t*)TDIndex;
		Buffer = (void*)kmalloc_a(PAGE_SIZE);
	}

	/* Set Link */
	Td->Link = UHCI_TD_LINK_END;

	/* Setup TD Control Status */
	Td->Flags = UHCI_TD_ACTIVE;
	Td->Flags |= UHCI_TD_SET_ERR_CNT(3);

	/* Low speed transfer? */
	if (Speed == LowSpeed)
		Td->Flags |= UHCI_TD_LOWSPEED;

	/* Isochronous TD? */
	if (Type == IsochronousTransfer)
		Td->Flags |= UHCI_TD_ISOCHRONOUS;

	/* In transfer? */
	if (PId == UHCI_TD_PID_IN)
		Td->Flags |= UHCI_TD_SHORT_PACKET;

	/* Setup TD Header Packet */
	Td->Header = PId;
	Td->Header |= UHCI_TD_DEVICE_ADDR(DeviceAddr);
	Td->Header |= UHCI_TD_EP_ADDR(EpAddr);

	/* Toggle? */
	if (Toggle)
		Td->Header |= UHCI_TD_DATA_TOGGLE;

	/* Set Size */
	if (Length > 0)
		Td->Header |= UHCI_TD_MAX_LEN((Length - 1));
	else
		Td->Header |= UHCI_TD_MAX_LEN(0x7FF);

	/* Set buffer */
	*TDBuffer = Buffer;
	Td->Buffer = AddressSpaceGetMap(AddressSpaceGetCurrent(), (VirtAddr_t)Buffer);

	/* Done */
	return Td;
}

/* Endpoint Functions */
void UhciEndpointSetup(void *Controller, UsbHcEndpoint_t *Endpoint)
{
	/* Cast */
	UhciController_t *uCtrl = (UhciController_t*)Controller;
	Addr_t BufAddr = 0, BufAddrMax = 0;
	Addr_t Pool, PoolPhys;
	size_t i;

	/* Allocate a structure */
	UhciEndpoint_t *uEp = (UhciEndpoint_t*)kmalloc(sizeof(UhciEndpoint_t));

	/* Construct the lock */
	SpinlockReset(&uEp->Lock);

	/* Woah */
	_CRT_UNUSED(uCtrl);

	/* Now, we want to allocate some TD's
	* but it largely depends on what kind of endpoint this is */
	if (Endpoint->Type == EndpointControl)
		uEp->TdsAllocated = UHCI_ENDPOINT_MIN_ALLOCATED;
	else if (Endpoint->Type == EndpointBulk)
	{
		/* Depends on the maximum transfer */
		uEp->TdsAllocated = DEVICEMANAGER_MAX_IO_SIZE / Endpoint->MaxPacketSize;

		/* Take in account control packets and other stuff */
		uEp->TdsAllocated += UHCI_ENDPOINT_MIN_ALLOCATED;
	}
	else
	{
		/* We handle interrupt & iso dynamically
		* we don't predetermine their sizes */
		uEp->TdsAllocated = 0;
		Endpoint->AttachedData = uEp;
		return;
	}

	/* Now, we do the actual allocation */
	uEp->TDPool = (UhciTransferDescriptor_t**)kmalloc(sizeof(UhciTransferDescriptor_t*) * uEp->TdsAllocated);
	uEp->TDPoolBuffers = (Addr_t**)kmalloc(sizeof(Addr_t*) * uEp->TdsAllocated);
	uEp->TDPoolPhysical = (Addr_t*)kmalloc(sizeof(Addr_t) * uEp->TdsAllocated);

	/* Allocate a TD block */
	Pool = (Addr_t)kmalloc((sizeof(UhciTransferDescriptor_t) * uEp->TdsAllocated) + UHCI_STRUCT_ALIGN);
	Pool = UhciAlign(Pool, UHCI_STRUCT_ALIGN_BITS, UHCI_STRUCT_ALIGN);
	PoolPhys = AddressSpaceGetMap(AddressSpaceGetCurrent(), Pool);

	/* Allocate buffers */
	BufAddr = (Addr_t)kmalloc_a(PAGE_SIZE);
	BufAddrMax = BufAddr + PAGE_SIZE - 1;

	/* Memset it */
	memset((void*)Pool, 0, sizeof(UhciTransferDescriptor_t) * uEp->TdsAllocated);

	/* Iterate it */
	for (i = 0; i < uEp->TdsAllocated; i++)
	{
		/* Set */
		uEp->TDPool[i] = (UhciTransferDescriptor_t*)Pool;
		uEp->TDPoolPhysical[i] = PoolPhys;

		/* Allocate another page? */
		if (BufAddr > BufAddrMax)
		{
			BufAddr = (Addr_t)kmalloc_a(PAGE_SIZE);
			BufAddrMax = BufAddr + PAGE_SIZE - 1;
		}

		/* Setup Buffer */
		uEp->TDPoolBuffers[i] = (Addr_t*)BufAddr;
		uEp->TDPool[i]->Buffer = AddressSpaceGetMap(AddressSpaceGetCurrent(), BufAddr);
		uEp->TDPool[i]->Link = UHCI_TD_LINK_END;

		/* Increase */
		Pool += sizeof(UhciTransferDescriptor_t);
		PoolPhys += sizeof(UhciTransferDescriptor_t);
		BufAddr += Endpoint->MaxPacketSize;
	}

	/* Done! Save */
	Endpoint->AttachedData = uEp;
}

void UhciEndpointDestroy(void *Controller, UsbHcEndpoint_t *Endpoint)
{
	/* Cast */
	UhciController_t *uCtrl = (UhciController_t*)Controller;
	UhciEndpoint_t *uEp = (UhciEndpoint_t*)Endpoint->AttachedData;

	/* Sanity */
	if (uEp == NULL)
		return;

	UhciTransferDescriptor_t *uTd = uEp->TDPool[0];
	size_t i;

	/* Woah */
	_CRT_UNUSED(uCtrl);

	/* Sanity */
	if (uEp->TdsAllocated != 0)
	{
		/* Let's free all those resources */
		for (i = 0; i < uEp->TdsAllocated; i++)
		{
			/* free buffer */
			kfree(uEp->TDPoolBuffers[i]);
		}

		/* Free blocks */
		kfree(uTd);
		kfree(uEp->TDPoolBuffers);
		kfree(uEp->TDPoolPhysical);
		kfree(uEp->TDPool);
	}

	/* Free the descriptor */
	kfree(uEp);
}

/* Bandwidth Functions */

/* Find highest current load for a given Phase/Period 
 * Used for calculating optimal bandwidth for a scheduler-queue */
int UhciCalculateBandwidth(UhciController_t *Controller, int Phase, int Period)
{
	/* Get current load */
	int HighestBw = Controller->Bandwidth[Phase];

	/* Iterate and check the bandwidth */
	for (Phase += Period; Phase < UHCI_BANDWIDTH_PHASES; Phase += Period)
		HighestBw = MAX(HighestBw, Controller->Bandwidth[Phase]);

	/* Done! */
	return HighestBw;
}

/* Max 90% of the bandwidth in a queue can go to iso/int 
 * thus we must make sure we don't go over that, this 
 * calculates if there is enough "room" for our Qh */
int UhciValidateBandwidth(UhciController_t *Controller, UhciQueueHead_t *Qh)
{
	/* Vars */
	int MinimalBw = 0;

	/* Find the optimal phase (unless it is already set) and get
	 * its load value. */
	if (Qh->Phase >= 0)
		MinimalBw = UhciCalculateBandwidth(Controller, Qh->Phase, Qh->Period);
	else 
	{
		/* Vars */
		int Phase, Bandwidth;
		int MaxPhase = MIN(UHCI_BANDWIDTH_PHASES, Qh->Period);

		/* Set initial */
		Qh->Phase = 0;
		MinimalBw = UhciCalculateBandwidth(Controller, Qh->Phase, Qh->Period);

		/* Iterate untill we locate the optimal phase */
		for (Phase = 1; Phase < MaxPhase; ++Phase)
		{
			/* Get bandwidth for this phase & period */
			Bandwidth = UhciCalculateBandwidth(Controller, Phase, Qh->Period);
			
			/* Sanity */
			if (Bandwidth < MinimalBw) {
				MinimalBw = Bandwidth;
				Qh->Phase = (uint16_t)Phase;
			}
		}
	}

	/* Maximum allowable periodic bandwidth is 90%, or 900 us per frame */
	if (MinimalBw + Qh->Bandwidth > 900)
		return -1;
	
	/* Done, Ok! */
	return 0;
}

/* Reserve Bandwidth */
void UhciReserveBandwidth(UhciController_t *Controller, UhciQueueHead_t *Qh)
{
	/* Vars */
	int Bandwidth = Qh->Bandwidth;
	int i;

	/* Iterate phase & period */
	for (i = Qh->Phase; i < UHCI_BANDWIDTH_PHASES; i += Qh->Period) {
		Controller->Bandwidth[i] += Bandwidth;
		Controller->TotalBandwidth += Bandwidth;
	}

	/* Set allocated */
	Qh->Flags |= UHCI_QH_BANDWIDTH_ALLOC;
}

/* Release Bandwidth */
void UhciReleaseBandwidth(UhciController_t *Controller, UhciQueueHead_t *Qh)
{
	/* Vars */
	int Bandwidth = Qh->Bandwidth;
	int i;

	/* Iterate and free */
	for (i = Qh->Phase; i < UHCI_BANDWIDTH_PHASES; i += Qh->Period) {
		Controller->Bandwidth[i] -= Bandwidth;
		Controller->TotalBandwidth -= Bandwidth;
	}
	
	/* Set not-allocated */
	Qh->Flags &= ~(UHCI_QH_BANDWIDTH_ALLOC);
}

/* Transaction Functions */

/* This one prepaires an ED */
void UhciTransactionInit(void *Controller, UsbHcRequest_t *Request)
{	
	/* Vars */
	UhciController_t *Ctrl = (UhciController_t*)Controller;
	UhciQueueHead_t *Qh = NULL;

	/* Get a QH */
	Qh = UhciAllocateQh(Ctrl, Request->Type);

	/* Save it */
	Request->Data = Qh;

	/* Set as not Completed for start */
	Request->Status = TransferNotProcessed;

	/* Do we need to reserve bandwidth? */
	if (Qh != NULL && 
		!(Qh->Flags & UHCI_QH_BANDWIDTH_ALLOC))
	{
		/* Vars */
		int Exponent, Queue, Run = 1;

		/* Find the correct index we link into */
		for (Exponent = 7; Exponent >= 0; --Exponent) {
			if ((1 << Exponent) <= (int)Request->Endpoint->Interval)
				break;
		}

		/* Sanity */
		if (Exponent < 0) {
			LogFatal("UHCI", "Invalid interval %u", Request->Endpoint->Interval);
			Exponent = 0;
		}

		/* Make sure we have enough bandwidth for the transfer */
		if (Exponent > 0) {
			while (Run != 0 && --Exponent >= 0)
			{
				/* Now we can select a queue */
				Queue = 9 - Exponent;

				/* Set initial period */
				Qh->Period = 1 << Exponent;

				/* Set Qh Queue */
				Qh->Flags = UHCI_QH_CLR_QUEUE(Qh->Flags);
				Qh->Flags |= UHCI_QH_SET_QUEUE(Queue);

				/* For now, interrupt phase is fixed by the layout
				* of the QH lists. */
				Qh->Phase = (Qh->Period / 2) & (UHCI_BANDWIDTH_PHASES - 1);

				/* Check */
				Run = UhciValidateBandwidth(Ctrl, Qh);
			}
		}
		else
		{
			/* Now we can select a queue */
			Queue = 9 - Exponent;

			/* Set initial period */
			Qh->Period = 1 << Exponent;

			/* Set Qh Queue */
			Qh->Flags = UHCI_QH_CLR_QUEUE(Qh->Flags);
			Qh->Flags |= UHCI_QH_SET_QUEUE(Queue);

			/* For now, interrupt phase is fixed by the layout
			* of the QH lists. */
			Qh->Phase = (Qh->Period / 2) & (UHCI_BANDWIDTH_PHASES - 1);

			/* Check */
			Run = UhciValidateBandwidth(Ctrl, Qh);
		}

		/* Sanity */
		if (Run != 0) {
			LogFatal("UHCI", "Had no room for the transfer in queueus");
			return;
		}

		/* Reserve the bandwidth */
		UhciReserveBandwidth(Ctrl, Qh);
	}
}

/* This one prepaires an setup Td */
UsbHcTransaction_t *UhciTransactionSetup(void *Controller, UsbHcRequest_t *Request)
{
	/* Vars */
	UhciController_t *Ctrl = (UhciController_t*)Controller;
	UsbHcTransaction_t *Transaction;

	/* Unused */
	_CRT_UNUSED(Ctrl);

	/* Allocate Transaction */
	Transaction = (UsbHcTransaction_t*)kmalloc(sizeof(UsbHcTransaction_t));
	Transaction->IoBuffer = 0;
	Transaction->IoLength = 0;
	Transaction->Link = NULL;

	/* Create the Td */
	Transaction->TransferDescriptor = (void*)UhciTdSetup(Request->Endpoint->AttachedData,
		Request->Type, &Request->Packet, 
		Request->Device->Address, Request->Endpoint->Address, 
		Request->Speed, &Transaction->TransferBuffer);

	/* Done! */
	return Transaction;
}

/* This one prepaires an in Td */
UsbHcTransaction_t *UhciTransactionIn(void *Controller, UsbHcRequest_t *Request)
{
	/* Variables */
	UhciController_t *Ctrl = (UhciController_t*)Controller;
	UsbHcTransaction_t *Transaction;

	/* Unused */
	_CRT_UNUSED(Ctrl);

	/* Allocate Transaction */
	Transaction = (UsbHcTransaction_t*)kmalloc(sizeof(UsbHcTransaction_t));
	Transaction->TransferDescriptorCopy = NULL;
	Transaction->IoBuffer = Request->IoBuffer;
	Transaction->IoLength = Request->IoLength;
	Transaction->Link = NULL;

	/* Setup Td */
	Transaction->TransferDescriptor = (void*)UhciTdIo(Request->Endpoint->AttachedData,
		Request->Type, Request->Endpoint->Toggle,
		Request->Device->Address, Request->Endpoint->Address, UHCI_TD_PID_IN, Request->IoLength,
		Request->Speed, &Transaction->TransferBuffer);

	/* If previous Transaction */
	if (Request->Transactions != NULL)
	{
		UhciTransferDescriptor_t *PrevTd;
		UsbHcTransaction_t *cTrans = Request->Transactions;

		while (cTrans->Link)
			cTrans = cTrans->Link;

		PrevTd = (UhciTransferDescriptor_t*)cTrans->TransferDescriptor;
		PrevTd->Link =
			(AddressSpaceGetMap(AddressSpaceGetCurrent(), 
				(VirtAddr_t)Transaction->TransferDescriptor) | UHCI_TD_LINK_DEPTH);
	}

	/* We might need a copy */
	if (Request->Type == InterruptTransfer
		|| Request->Type == IsochronousTransfer)
	{
		/* Allocate TD */
		Transaction->TransferDescriptorCopy =
			(void*)UhciAlign(((Addr_t)kmalloc(sizeof(UhciTransferDescriptor_t) + UHCI_STRUCT_ALIGN)), UHCI_STRUCT_ALIGN_BITS, UHCI_STRUCT_ALIGN);

		/* Copy data */
		memcpy(Transaction->TransferDescriptorCopy, 
			Transaction->TransferDescriptor, sizeof(UhciTransferDescriptor_t));
	}

	/* Done */
	return Transaction;
}

/* This one prepaires an out Td */
UsbHcTransaction_t *UhciTransactionOut(void *Controller, UsbHcRequest_t *Request)
{
	/* Vars */
	UhciController_t *Ctrl = (UhciController_t*)Controller;
	UsbHcTransaction_t *Transaction;

	/* Unused */
	_CRT_UNUSED(Ctrl);

	/* Allocate Transaction */
	Transaction = (UsbHcTransaction_t*)kmalloc(sizeof(UsbHcTransaction_t));
	Transaction->TransferDescriptorCopy = NULL;
	Transaction->IoBuffer = 0;
	Transaction->IoLength = 0;
	Transaction->Link = NULL;

	/* Setup Td */
	Transaction->TransferDescriptor = (void*)UhciTdIo(Request->Endpoint->AttachedData,
		Request->Type, Request->Endpoint->Toggle,
		Request->Device->Address, Request->Endpoint->Address, UHCI_TD_PID_OUT, Request->IoLength,
		Request->Speed, &Transaction->TransferBuffer);

	/* Copy Data */
	if (Request->IoBuffer != NULL && Request->IoLength != 0)
		memcpy(Transaction->TransferBuffer, Request->IoBuffer, Request->IoLength);

	/* If previous Transaction */
	if (Request->Transactions != NULL)
	{
		UhciTransferDescriptor_t *PrevTd;
		UsbHcTransaction_t *cTrans = Request->Transactions;

		while (cTrans->Link)
			cTrans = cTrans->Link;

		PrevTd = (UhciTransferDescriptor_t*)cTrans->TransferDescriptor;
		PrevTd->Link =
			(AddressSpaceGetMap(AddressSpaceGetCurrent(), (VirtAddr_t)Transaction->TransferDescriptor) | UHCI_TD_LINK_DEPTH);
	}

	/* We might need a copy */
	if (Request->Type == InterruptTransfer
		|| Request->Type == IsochronousTransfer)
	{
		/* Allocate TD */
		Transaction->TransferDescriptorCopy =
			(void*)UhciAlign(((Addr_t)kmalloc(sizeof(UhciTransferDescriptor_t) + UHCI_STRUCT_ALIGN)), UHCI_STRUCT_ALIGN_BITS, UHCI_STRUCT_ALIGN);

		/* Copy data */
		memcpy(Transaction->TransferDescriptorCopy,
			Transaction->TransferDescriptor, sizeof(UhciTransferDescriptor_t));
	}

	/* Done */
	return Transaction;
}

/* This one queues the Transaction up for processing */
void UhciTransactionSend(void *Controller, UsbHcRequest_t *Request)
{
	/* Wuhu */
	UsbHcTransaction_t *Transaction = Request->Transactions;
	UhciController_t *Ctrl = (UhciController_t*)Controller;
	UsbTransferStatus_t Completed;
	UhciQueueHead_t *Qh = NULL;
	UhciTransferDescriptor_t *Td = NULL;
	Addr_t QhAddress = 0;
	int CondCode;

	/* Cast */
	Qh = (UhciQueueHead_t*)Request->Data;

	/* Get physical */
	QhAddress = AddressSpaceGetMap(AddressSpaceGetCurrent(), (VirtAddr_t)Qh);
	
	/* Set as not Completed for start */
	Request->Status = TransferNotProcessed;

#ifdef UHCI_DIAGNOSTICS
	Transaction = Request->Transactions;
	while (Transaction->Link)
	{
		/* Next */
		Transaction = Transaction->Link;

		Td = (UhciTransferDescriptor_t*)Transaction->TransferDescriptor;
		LogDebug("UHCI", "Td (Addr 0x%x) Flags 0x%x, Buffer 0x%x, Header 0x%x, Next Td 0x%x",
			AddressSpaceGetMap(AddressSpaceGetCurrent(), (VirtAddr_t)Td),
			Td->Flags, Td->Buffer, Td->Header, Td->Link);
	}
#endif

	/* Iterate and set last to generate interrupt */
	Transaction = Request->Transactions;
	while (Transaction->Link)
		Transaction = Transaction->Link;

	/* Set last to generate IOC */
#ifndef UHCI_DIAGNOSTICS
	Td = (UhciTransferDescriptor_t*)Transaction->TransferDescriptor;
	Td->Flags |= UHCI_TD_IOC;
#endif

	/* Initialize QH */
	UhciQhInit(Ctrl, Qh, Request->Transactions);

	/* Set true */
	Completed = TransferFinished;

	/* Add this Transaction to list */
	list_append((list_t*)Ctrl->TransactionList, list_create_node(0, Request));

	/* Debug */
#ifdef UHCI_DIAGNOSTICS
	if (Request->Type != IsochronousTransfer)
		LogDebug("UHCI", "QH at 0x%x, FirstTd 0x%x, NextQh 0x%x", QhAddress, Qh->Child, Qh->Link);
#endif

	/* Now lets try the Transaction */
	SpinlockAcquire(&Ctrl->Lock);

	/* For Control & Bulk, we link into async list */
	if (Request->Type == ControlTransfer
		|| Request->Type == BulkTransfer)
	{
		/* Stop controller */
		UhciStop(Ctrl);

		/* If it's a low-speed control, we append to async 
		 * If it's a high-speed control, we insert at the front of the fsbr 
		 * If it's a high-speed bulk, we append to fsbr */

		/* Just append to async */
		UhciQueueHead_t *PrevQh = Ctrl->QhPool[UHCI_POOL_ASYNC];

		/* Iterate to end */
		while (PrevQh->LinkVirtual != 0
			&& PrevQh->LinkVirtual != (uint32_t)Ctrl->QhPool[UHCI_POOL_FSBR]) {
			PrevQh = (UhciQueueHead_t*)PrevQh->LinkVirtual;
		}

		/* Insert */
		PrevQh->Link = (QhAddress | UHCI_TD_LINK_QH);
		PrevQh->LinkVirtual = (uint32_t)Qh;
	}
	else if (Request->Type == InterruptTransfer)
	{
		/* Get queue */
		UhciQueueHead_t *PrevQh = NULL;
		int Queue = UHCI_QT_GET_QUEUE(Qh->Flags);

		/* Iterate */
		Transaction = Request->Transactions;
		Td = (UhciTransferDescriptor_t*)Transaction->TransferDescriptor;
		LogDebug("UHCI", "Td (Addr 0x%x) Flags 0x%x, Buffer 0x%x, Header 0x%x, Next Td 0x%x",
			AddressSpaceGetMap(AddressSpaceGetCurrent(), (VirtAddr_t)Td),
			Td->Flags, Td->Buffer, Td->Header, Td->Link);

		while (Transaction->Link)
		{
			/* Next */
			Transaction = Transaction->Link;

			Td = (UhciTransferDescriptor_t*)Transaction->TransferDescriptor;
			LogDebug("UHCI", "Td (Addr 0x%x) Flags 0x%x, Buffer 0x%x, Header 0x%x, Next Td 0x%x",
				AddressSpaceGetMap(AddressSpaceGetCurrent(), (VirtAddr_t)Td),
				Td->Flags, Td->Buffer, Td->Header, Td->Link);
		}

		/* Iterate list */
		PrevQh = Ctrl->QhPool[Queue];

		/* Insert */
		Qh->Link = PrevQh->Link | UHCI_TD_LINK_QH;
		Qh->LinkVirtual = PrevQh->LinkVirtual;

		/* Set link - Atomic operation 
		 * We don't need to stop/start controller */
		PrevQh->Link = QhAddress | UHCI_TD_LINK_QH;
		PrevQh->LinkVirtual = (uint32_t)Qh;

		/* Release lock */
		SpinlockRelease(&Ctrl->Lock);

		/* Done! */
		return;
	}
	else
	{
		/* Isochronous Transfer */

		/* Get current frame */
		//uint16_t CurrentFrame = UhciRead16(Ctrl, UHCI_REGISTER_FRNUM) & 0x7FF;

		/* Calculate where to put it */
		//CurrentFrame += 10;

		/* Insert directly into the frame and link to the QH */

		/* Fixup toggles ? */

		/* Done! */
		return;
	}

	/* Release lock */
	SpinlockRelease(&Ctrl->Lock);

	/* Start controller */
	UhciStart(Ctrl);

	/* Wait for interrupt */
#ifndef UHCI_DIAGNOSTICS
	
	/* Sleep untill completion */
	SchedulerSleepThread((Addr_t*)Request->Data);

	/* Yield */
	_ThreadYield();

#else
	/* Wait */
	StallMs(100);
#endif

	/* Check Conditions */
#ifdef UHCI_DIAGNOSTICS
	LogDebug("UHCI", "Qh Next 0x%x, Qh Head 0x%x", Qh->Link, Qh->Child);
#endif
	Transaction = Request->Transactions;
	while (Transaction)
	{
		/* Cast and get the transfer code */
		Td = (UhciTransferDescriptor_t*)Transaction->TransferDescriptor;
		CondCode = UhciConditionCodeToIndex(UHCI_TD_STATUS(Td->Flags));
#ifdef UHCI_DIAGNOSTICS
		LogDebug("UHCI", "Td Flags 0x%x, Header 0x%x, Buffer 0x%x, Td Condition Code %u (%s)", 
			Td->Flags, Td->Header, Td->Buffer, CondCode, UhciErrorMessages[CondCode]);
#endif

		if (CondCode == 0 && Completed == TransferFinished)
			Completed = TransferFinished;
		else
		{
			if (CondCode == 6)
				Completed = TransferStalled;
			else if (CondCode == 1)
				Completed = TransferInvalidToggles;
			else if (CondCode == 2)
				Completed = TransferNotResponding;
			else if (CondCode == 3)
				Completed = TransferNAK;
			else {
				LogDebug("UHCI", "Error: 0x%x (%s)", CondCode, UhciErrorMessages[CondCode]);
				Completed = TransferInvalidData;
			}
			break;
		}

		Transaction = Transaction->Link;
	}

	/* Lets see... */
	if (Completed == TransferFinished)
	{
		/* Build Buffer */
		Transaction = Request->Transactions;
		while (Transaction)
		{
			/* Copy Data? */
			if (Transaction->IoBuffer != NULL && Transaction->IoLength != 0)
			{
				//printf("Buffer Copy 0x%x, Length 0x%x\n", Transaction->IoBuffer, Transaction->IoLength);
				memcpy(Transaction->IoBuffer, Transaction->TransferBuffer, Transaction->IoLength);
			}

			/* Next Link */
			Transaction = Transaction->Link;
		}
	}

	/* Update Status */
	Request->Status = Completed;

#ifdef UHCI_DIAGNOSTICS
	for (;;);
#endif
}

/* Cleans up transfers */
void UhciTransactionDestroy(void *Controller, UsbHcRequest_t *Request)
{
	/* Cast, we need these */
	UsbHcTransaction_t *Transaction = Request->Transactions;
	UhciController_t *Ctrl = (UhciController_t*)Controller;

	/* Lock controller */
	SpinlockAcquire(&Ctrl->Lock);

	/* Stop controller */
	UhciStop(Ctrl);

	/* Unallocate Qh */
	if (Request->Type == ControlTransfer
		|| Request->Type == BulkTransfer)
	{
		/* Cast Qh */
		UhciQueueHead_t *Qh = (UhciQueueHead_t*)Request->Data;

		/* Unlink Qh */
		UhciQueueHead_t *PrevQh = Ctrl->QhPool[UHCI_POOL_ASYNC];

		/* Iterate to end */
		while (PrevQh->LinkVirtual != (uint32_t)Qh)
			PrevQh = (UhciQueueHead_t*)PrevQh->LinkVirtual;

		/* Sanity */
		if (PrevQh->LinkVirtual != (uint32_t)Qh) {
			LogDebug("UHCI", "Couldn't find Qh in frame-list");
		}
		else 
		{
			/* Now skip */
			PrevQh->Link = Qh->Link;
			PrevQh->LinkVirtual = Qh->LinkVirtual;

			/* Iterate and reset */
			while (Transaction)
			{
				/* Memset */
				memset(Transaction->TransferDescriptor, 0, sizeof(UhciTransferDescriptor_t));

				/* Next */
				Transaction = Transaction->Link;
			}

			/* Invalidate links */
			Qh->Child = 0;
			Qh->ChildVirtual = 0;
			Qh->Link = 0;
			Qh->LinkVirtual = 0;

			/* Mark inactive */
			Qh->Flags &= ~UHCI_QH_ACTIVE;
		}
	}
	else if (Request->Type == InterruptTransfer)
	{
		/* Vars */
		UhciQueueHead_t *TargetQh = (UhciQueueHead_t*)Request->Data;
		UhciQueueHead_t *Qh = NULL, *PrevQh = NULL;
		int Queue = UHCI_QT_GET_QUEUE(Qh->Flags);

		/* Iterate and find our Qh */
		Qh = Ctrl->QhPool[Queue];
		while (Qh != TargetQh) {
			PrevQh = Qh;
			Qh = (UhciQueueHead_t*)Qh->LinkVirtual;

			/* Sanity */
			if (Qh == Ctrl->QhPool[UHCI_POOL_ASYNC]) {
				Qh = NULL;
				break;
			}
		}

		/* If Qh is null, didn't exist */
		if (Qh == NULL) {
			LogDebug("UHCI", "Tried to unschedule a queue-qh that didn't exist in queue");
		}
		else
		{
			/* If there is a previous (there should always be) */
			if (PrevQh != NULL) {
				PrevQh->Link = TargetQh->Link;
				PrevQh->LinkVirtual = TargetQh->LinkVirtual;
			} 
		}

		/* Free bandwidth */
		UhciReleaseBandwidth(Ctrl, Qh);

		/* Iterate transactions and free buffers & td's */
		while (Transaction)
		{
			/* free buffer */
			kfree(Transaction->TransferBuffer);

			/* free both TD's */
			kfree((void*)Transaction->TransferDescriptor);
			kfree((void*)Transaction->TransferDescriptorCopy);

			/* Next */
			Transaction = Transaction->Link;
		}

		/* Free it */
		kfree(Request->Data);
	}
	else
	{
		/* Cast Qh */
		UhciQueueHead_t *Qh = (UhciQueueHead_t*)Request->Data;

		/* Get frame where it's linked */

		/* Remove them and fixup toggles */

		/* Free bandwidth */
		UhciReleaseBandwidth(Ctrl, Qh);

		/* Iterate transactions and free buffers & td's */
		while (Transaction)
		{
			/* free buffer */
			kfree(Transaction->TransferBuffer);

			/* free both TD's */
			kfree((void*)Transaction->TransferDescriptor);
			kfree((void*)Transaction->TransferDescriptorCopy);

			/* Next */
			Transaction = Transaction->Link;
		}
	}

	/* Done */
	SpinlockRelease(&Ctrl->Lock);

	/* Enable Controller */
	UhciStart(Ctrl);
}

/* Handles completions */
void UhciProcessTransfers(UhciController_t *Controller)
{
	/* Transaction is completed / Failed */
	list_t *Transactions = (list_t*)Controller->TransactionList;
	int Completed = 0;

	/* Get transactions in progress and find the offender */
	foreach(Node, Transactions)
	{
		/* Cast UsbRequest */
		UsbHcRequest_t *HcRequest = (UsbHcRequest_t*)Node->data;

		/* Get transaction list */
		UsbHcTransaction_t *tList = (UsbHcTransaction_t*)HcRequest->Transactions;

		/* Loop through transactions */
		while (tList)
		{
			/* Cast Td */
			UhciTransferDescriptor_t *Td = 
				(UhciTransferDescriptor_t*)tList->TransferDescriptor;

			/* Get code */
			int CondCode = UhciConditionCodeToIndex(UHCI_TD_STATUS(Td->Flags));
			int ErrCount = UHCI_TD_ERROR_COUNT(Td->Flags);

			/* Sanity first */
			if (Td->Flags & UHCI_TD_ACTIVE) {
				Completed = 0;
				break;
			}

			/* Set completed per default */
			Completed = 1;

			/* Error Transfer ?
			 * No need to check rest */
			if (CondCode != 0
				&& CondCode != 3) {
				if (HcRequest->Type == InterruptTransfer)
					LogDebug("UHCI", "Td Error: Td Flags 0x%x, Header 0x%x, Error Count: %i",
						Td->Flags, Td->Header, ErrCount);
				break;
			}

			/* Get next transaction */
			tList = tList->Link;
		}

		/* Was it a completed/failed transaction ? ? */
		if (Completed)
		{
			/* What kind of transfer was this? */
			if (HcRequest->Type == ControlTransfer
				|| HcRequest->Type == BulkTransfer)
			{
				/* Wake a node */
				SchedulerWakeupOneThread((Addr_t*)HcRequest->Data);

				/* Remove from list */
				list_remove_by_node(Transactions, Node);

				/* Cleanup node */
				kfree(Node);
			}
			else
			{
				/* Re-Iterate */
				UsbHcTransaction_t *lIterator = HcRequest->Transactions;

				/* Copy data if not dummy */
				while (lIterator)
				{
					/* Let's see */
					if (lIterator->IoLength != 0)
					{
						/* Copy Data from transfer buffer to IoBuffer */
						memcpy(lIterator->IoBuffer, lIterator->TransferBuffer, lIterator->IoLength);
					}

					/* Switch toggle */
					if (HcRequest->Type == InterruptTransfer)
					{
						UhciTransferDescriptor_t *__Td =
							(UhciTransferDescriptor_t*)lIterator->TransferDescriptorCopy;

						/* If set */
						if (__Td->Header & UHCI_TD_DATA_TOGGLE)
							__Td->Header &= ~UHCI_TD_DATA_TOGGLE;
						else
							__Td->Header |= UHCI_TD_DATA_TOGGLE;
					}

					/* Restart Td */
					memcpy(lIterator->TransferDescriptor,
						lIterator->TransferDescriptorCopy, sizeof(UhciTransferDescriptor_t));

					/* Eh, next link */
					lIterator = lIterator->Link;
				}

				/* Callback */
				HcRequest->Callback->Callback(HcRequest->Callback->Args);
			}

			/* Done */
			break;
		}
	}
}

/* Interrupt Handler */
int UhciInterruptHandler(void *Args)
{
	/* Vars */
	MCoreDevice_t *mDevice = (MCoreDevice_t*)Args;
	UhciController_t *Controller = (UhciController_t*)mDevice->Driver.Data;
	uint16_t IntrState = 0;

	/* Get INTR state */
	IntrState = UhciRead16(Controller, UHCI_REGISTER_STATUS);
	
	/* Did this one come from us? */
	if (!(IntrState & 0x1F))
		return X86_IRQ_NOT_HANDLED;

#ifdef UHCI_DIAGNOSTICS
	/* Debug */
	LogInformation("UHCI", "INTERRUPT Controller %u: 0x%x", Controller->Id, IntrState);
#endif

	/* Clear Interrupt Bits :-) */
	UhciWrite16(Controller, UHCI_REGISTER_STATUS, IntrState);

	/* So.. Either completion or failed */
	if (IntrState & (UHCI_STATUS_USBINT | UHCI_STATUS_INTR_ERROR))
		UhciProcessTransfers(Controller);

	/* Resume Detected */
	if (IntrState & UHCI_STATUS_RESUME_DETECT)
	{
		/* Send run command */
		uint16_t OldCmd = UhciRead16(Controller, UHCI_REGISTER_COMMAND);
		OldCmd |= (UHCI_CMD_CONFIGFLAG | UHCI_CMD_RUN | UHCI_CMD_MAXPACKET64);
		UhciWrite16(Controller, UHCI_REGISTER_COMMAND, OldCmd);
	}

	/* Host Error */
	if (IntrState & UHCI_STATUS_HOST_SYSERR)
	{
		/* Reset Controller */
		UsbEventCreate(UsbGetHcd(Controller->Id), 0, HcdFatalEvent);
	}

	/* TD Processing Error */
	if (IntrState & UHCI_STATUS_PROCESS_ERR)
	{
		/* Fatal Error 
		 * Unschedule TDs and restart controller */
		LogInformation("UHCI", "Processing Error :/");
	}

	/* Done! */
	return X86_IRQ_HANDLED;
}
