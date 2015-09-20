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
* MollenOS X86-32 USB OHCI Controller Driver
* Todo:
* Isochronous Support
* Stability (Only tested on emulators and one real hardware pc).
* Multiple Transfers per interrupt (should be easy)
*/

/* Includes */
#include <Arch.h>
#include <LApic.h>
#include <assert.h>
#include <Memory.h>
#include <Scheduler.h>
#include <Heap.h>
#include <List.h>
#include <stdio.h>
#include <string.h>
#include <SysTimers.h>

#include <Drivers\Usb\Usb.h>
#include <Drivers\Usb\Ohci\Ohci.h>

/* Globals */
volatile uint32_t GlbOhciControllerId = 0;

/* Externs */
extern void _yield(void);

/* Prototypes */
int OhciInterruptHandler(void *data);
void OhciReset(OhciController_t *Controller);
void OhciSetup(OhciController_t *Controller);

uint32_t OhciAllocateTd(OhciController_t *Controller);

void OhciTransactionInit(void *Controller, UsbHcRequest_t *Request);
UsbHcTransaction_t *OhciTransactionSetup(void *Controller, UsbHcRequest_t *Request);
UsbHcTransaction_t *OhciTransactionIn(void *Controller, UsbHcRequest_t *Request);
UsbHcTransaction_t *OhciTransactionOut(void *Controller, UsbHcRequest_t *Request);
void OhciTransactionSend(void *Controller, UsbHcRequest_t *Request);

void OhciInstallInterrupt(void *Controller, UsbHcDevice_t *Device, UsbHcEndpoint_t *Endpoint,
	void *InBuffer, size_t InBytes, void(*Callback)(void*, size_t), void *Arg);

/* Error Codes */
static const int _Balance[] = { 0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15 };
const char *OhciErrorMessages[] = 
{
	"No Error",
	"CRC Error",
	"Bit Stuffing Violation",
	"Data Toggle Mismatch",
	"Stall PID recieved",
	"Device Not Responding",
	"PID Check Failure",
	"Unexpected PID",
	"Data Overrun",
	"Data Underrun",
	"Reserved",
	"Reserved",
	"Buffer Overrun",
	"Buffer Underrun",
	"Not Accessed",
	"Not Accessed"
};


/* Helpers */
void OhciSetMode(OhciController_t *Controller, uint32_t Mode)
{
	/* First we clear the current Operation Mode */
	uint32_t Val = Controller->Registers->HcControl;
	Val = (Val & ~X86_OHCI_CTRL_USB_SUSPEND);
	Val |= Mode;
	Controller->Registers->HcControl = Val;
}

Addr_t OhciAlign(Addr_t Address, Addr_t AlignmentBitMask, Addr_t Alignment)
{
	Addr_t AlignedAddr = Address;

	if (AlignedAddr & AlignmentBitMask)
	{
		AlignedAddr &= ~AlignmentBitMask;
		AlignedAddr += Alignment;
	}

	return AlignedAddr;
}

/* Stop/Start */
void OhciStop(OhciController_t *Controller)
{
	uint32_t Temp;

	/* Disable BULK and CONTROL queues */
	Temp = Controller->Registers->HcControl;
	Temp = (Temp & ~0x00000030);
	Controller->Registers->HcControl = Temp;

	/* Tell Command Status we dont have the list filled */
	Temp = Controller->Registers->HcCommandStatus;
	Temp = (Temp & ~0x00000006);
	Controller->Registers->HcCommandStatus = Temp;
}

/* This resets a Port, this is only ever
* called from an interrupt and thus we can't use StallMs :/ */
void OhciPortReset(OhciController_t *Controller, uint32_t Port)
{
	/* Set reset */
	Controller->Registers->HcRhPortStatus[Port] = (X86_OHCI_PORT_RESET);

	/* Wait with timeout */
	WaitForCondition((Controller->Registers->HcRhPortStatus[Port] & X86_OHCI_PORT_RESET) == 0, 200, 10, "USB_OHCI: Failed to reset device on port %u\n", Port);

	/* Set Enable */
	if (Controller->PowerMode == X86_OHCI_POWER_PORT_CONTROLLED)
		Controller->Registers->HcRhPortStatus[Port] = X86_OHCI_PORT_ENABLED | X86_OHCI_PORT_POWER_ENABLE;
	else
		Controller->Registers->HcRhPortStatus[Port] = X86_OHCI_PORT_ENABLED;

	/* Stall */
	StallMs(50);
}

/* Callbacks */
void OhciPortStatus(void *ControllerData, UsbHcPort_t *Port)
{
	OhciController_t *Controller = (OhciController_t*)ControllerData;
	uint32_t Status;

	/* Wait for power to stabilize */
	StallMs(Controller->PowerOnDelayMs);

	/* Reset Port */
	OhciPortReset(Controller, Port->Id);

	/* Update information in Port */
	Status = Controller->Registers->HcRhPortStatus[Port->Id];

	/* Is it connected? */
	if (Status & X86_OHCI_PORT_CONNECTED)
		Port->Connected = 1;
	else
		Port->Connected = 0;

	/* Is it enabled? */
	if (Status & X86_OHCI_PORT_ENABLED)
		Port->Enabled = 1;
	else
		Port->Enabled = 0;

	/* Is it full-speed? */
	if (Status & X86_OHCI_PORT_LOW_SPEED)
		Port->FullSpeed = 0;
	else
		Port->FullSpeed = 1;

	/* Clear Connect Event */
	if (Controller->Registers->HcRhPortStatus[Port->Id] & X86_OHCI_PORT_CONNECT_EVENT)
		Controller->Registers->HcRhPortStatus[Port->Id] = X86_OHCI_PORT_CONNECT_EVENT;

	/* If Enable Event bit is set, clear it */
	if (Controller->Registers->HcRhPortStatus[Port->Id] & X86_OHCI_PORT_ENABLE_EVENT)
		Controller->Registers->HcRhPortStatus[Port->Id] = X86_OHCI_PORT_ENABLE_EVENT;

	/* If Suspend Event is set, clear it */
	if (Controller->Registers->HcRhPortStatus[Port->Id] & X86_OHCI_PORT_SUSPEND_EVENT)
		Controller->Registers->HcRhPortStatus[Port->Id] = X86_OHCI_PORT_SUSPEND_EVENT;

	/* If Over Current Event is set, clear it */
	if (Controller->Registers->HcRhPortStatus[Port->Id] & X86_OHCI_PORT_OVR_CURRENT_EVENT)
		Controller->Registers->HcRhPortStatus[Port->Id] = X86_OHCI_PORT_OVR_CURRENT_EVENT;

	/* If reset bit is set, clear it */
	if (Controller->Registers->HcRhPortStatus[Port->Id] & X86_OHCI_PORT_RESET_EVENT)
		Controller->Registers->HcRhPortStatus[Port->Id] = X86_OHCI_PORT_RESET_EVENT;
}

/* Port Functions */
void OhciPortCheck(OhciController_t *Controller, uint32_t Port)
{
	UsbHc_t *HcCtrl;

	/* Was it connect event and not disconnect ? */
	if (Controller->Registers->HcRhPortStatus[Port] & X86_OHCI_PORT_CONNECT_EVENT)
	{
		if (Controller->Registers->HcRhPortStatus[Port] & X86_OHCI_PORT_CONNECTED)
		{
			/* Reset on Attach */
			OhciPortReset(Controller, Port);
		}
		else
		{
			/* Nah, disconnect event */

			/* Get HCD data */
			HcCtrl = UsbGetHcd(Controller->HcdId);

			/* Sanity */
			if (HcCtrl == NULL)
				return;

			/* Disconnect */
			UsbEventCreate(HcCtrl, Port, X86_USB_EVENT_DISCONNECTED);
		}

		/* If Device is enabled, and powered, set it up */
		if ((Controller->Registers->HcRhPortStatus[Port] & X86_OHCI_PORT_ENABLED)
			&& (Controller->Registers->HcRhPortStatus[Port] & X86_OHCI_PORT_POWER_ENABLE))
		{
			/* Get HCD data */
			HcCtrl = UsbGetHcd(Controller->HcdId);

			/* Sanity */
			if (HcCtrl == NULL)
			{
				printf("OHCI: Controller %u is zombie and is trying to register Ports!!\n", Controller->Id);
				return;
			}

			/* Register Device */
			UsbEventCreate(HcCtrl, Port, X86_USB_EVENT_CONNECTED);
		}
	}

	/* Clear Connect Event */
	if (Controller->Registers->HcRhPortStatus[Port] & X86_OHCI_PORT_CONNECT_EVENT)
		Controller->Registers->HcRhPortStatus[Port] = X86_OHCI_PORT_CONNECT_EVENT;

	/* If Enable Event bit is set, clear it */
	if (Controller->Registers->HcRhPortStatus[Port] & X86_OHCI_PORT_ENABLE_EVENT)
		Controller->Registers->HcRhPortStatus[Port] = X86_OHCI_PORT_ENABLE_EVENT;

	/* If Suspend Event is set, clear it */
	if (Controller->Registers->HcRhPortStatus[Port] & X86_OHCI_PORT_SUSPEND_EVENT)
		Controller->Registers->HcRhPortStatus[Port] = X86_OHCI_PORT_SUSPEND_EVENT;

	/* If Over Current Event is set, clear it */
	if (Controller->Registers->HcRhPortStatus[Port] & X86_OHCI_PORT_OVR_CURRENT_EVENT)
		Controller->Registers->HcRhPortStatus[Port] = X86_OHCI_PORT_OVR_CURRENT_EVENT;

	/* If reset bit is set, clear it */
	if (Controller->Registers->HcRhPortStatus[Port] & X86_OHCI_PORT_RESET_EVENT)
		Controller->Registers->HcRhPortStatus[Port] = X86_OHCI_PORT_RESET_EVENT;
}

void OhciPortsCheck(OhciController_t *Controller)
{
	int i;

	/* Go through Ports */
	for (i = 0; i < (int)Controller->Ports; i++)
	{
		/* Check íf Port has connected */
		OhciPortCheck(Controller, i);
	}
}

/* Function Allocates Resources 
 * and starts a init thread */
void OhciInit(PciDevice_t *Device)
{
	uint16_t PciCommand;
	OhciController_t *Controller = NULL;
	
	/* Allocate Resources for this Controller */
	Controller = (OhciController_t*)kmalloc(sizeof(OhciController_t));
	Controller->PciDevice = Device;
	Controller->Id = GlbOhciControllerId;

	/* Enable memory and Bus mastering */
	PciCommand = PciReadWord((const uint16_t)Device->Bus, (const uint16_t)Device->Device, (const uint16_t)Device->Function, 0x4);
	PciWriteWord((const uint16_t)Device->Bus, (const uint16_t)Device->Device, (const uint16_t)Device->Function, 0x4, (PciCommand & ~(0x400)) | 0x2 | 0x4);

	/* Get location of Registers */
	Controller->ControlSpace = Device->Header->Bar0;

	/* Sanity */
	if (Controller->ControlSpace == 0 || (Controller->ControlSpace & 0x1))
	{
		/* Yea, give me my hat back */
		kfree(Controller);
		return;
	}

	/* Now we initialise */
	GlbOhciControllerId++;

	/* Memory map needed space */
	Controller->Registers = (volatile OhciRegisters_t*)MmVirtualMapSysMemory(Controller->ControlSpace, 1);
	Controller->HccaSpace = (uint32_t)MmPhysicalAllocateBlockDma();
	Controller->HCCA = (volatile OhciHCCA_t*)Controller->HccaSpace;
	SpinlockReset(&Controller->Lock);

	/* Memset HCCA Space */
	memset((void*)Controller->HCCA, 0, 0x1000);

	/* Install IRQ Handler */
	InterruptInstallPci(Device, OhciInterruptHandler, Controller);

	/* Debug */
	printf("OHCI - Id %u, bar0: 0x%x (0x%x), dma: 0x%x\n", 
		Controller->Id, Controller->ControlSpace,
		(Addr_t)Controller->Registers, Controller->HccaSpace);

	/* Reset Controller */
	OhciSetup(Controller);
}

/* Initializes Controller Queues */
void OhciInitQueues(OhciController_t *Controller)
{
	Addr_t BufAddr = 0, BufAddrMax = 0;
	Addr_t Pool = 0, PoolPhys = 0;
	Addr_t EdLevel;
	int i;

	/* Create the NULL Td */
	Controller->NullTd = (OhciGTransferDescriptor_t*)OhciAlign(((Addr_t)kmalloc(sizeof(OhciGTransferDescriptor_t) + X86_OHCI_STRUCT_ALIGN)), X86_OHCI_STRUCT_ALIGN_BITS, X86_OHCI_STRUCT_ALIGN);
	Controller->NullTd->BufferEnd = 0;
	Controller->NullTd->Cbp = 0;
	Controller->NullTd->NextTD = 0x0;
	Controller->NullTd->Flags = 0;

	/* Initialise ED Pool */
	Controller->EDIndexControl = 0;
	Controller->EDIndexBulk = X86_OHCI_POOL_CONTROL_EDS;
	for (i = 0; i < X86_OHCI_POOL_NUM_ED; i++)
	{
		/* Allocate */
		Addr_t aSpace = (Addr_t)kmalloc(sizeof(OhciEndpointDescriptor_t) + X86_OHCI_STRUCT_ALIGN);
		Controller->EDPool[i] = (OhciEndpointDescriptor_t*)OhciAlign(aSpace, X86_OHCI_STRUCT_ALIGN_BITS, X86_OHCI_STRUCT_ALIGN);
		
		/* Zero it out */
		memset((void*)Controller->EDPool[i], 0, sizeof(OhciEndpointDescriptor_t));
		
		/* Link to previous */
		Controller->EDPool[i]->NextED = 0;
		
		/* Set to skip and valid null Td */
		Controller->EDPool[i]->Flags = X86_OHCI_EP_SKIP;
		Controller->EDPool[i]->HeadPtr = 
			(Controller->EDPool[i]->TailPtr = MmVirtualGetMapping(NULL, (Addr_t)Controller->NullTd)) | 0x1;
	}

	/* Initialise Bulk/Control Td Pool & Buffers */
	Controller->TDIndex = 0;
	BufAddr = (Addr_t)kmalloc_a(0x1000);
	BufAddrMax = BufAddr + 0x1000 - 1;
	
	/* Allocate Pool of 100 Td's */
	Pool = (Addr_t)kmalloc((sizeof(OhciGTransferDescriptor_t) * X86_OHCI_POOL_NUM_TD) + X86_OHCI_STRUCT_ALIGN);
	Pool = OhciAlign(Pool, X86_OHCI_STRUCT_ALIGN_BITS, X86_OHCI_STRUCT_ALIGN);
	PoolPhys = MmVirtualGetMapping(NULL, Pool);

	/* Memset it */
	memset((void*)Pool, 0, sizeof(OhciGTransferDescriptor_t) * X86_OHCI_POOL_NUM_TD);

	/* Iterate it */
	for (i = 0; i < X86_OHCI_POOL_NUM_TD; i++)
	{
		/* Set */
		Controller->TDPool[i] = (OhciGTransferDescriptor_t*)Pool;
		Controller->TDPoolPhys[i] = PoolPhys;

		/* Allocate another page? */
		if (BufAddr > BufAddrMax)
		{
			BufAddr = (Addr_t)kmalloc_a(0x1000);
			BufAddrMax = BufAddr + 0x1000 - 1;
		}

		/* Setup Buffer */
		Controller->TDPoolBuffers[i] = (Addr_t*)BufAddr;
		Controller->TDPool[i]->Cbp = MmVirtualGetMapping(NULL, BufAddr);
		Controller->TDPool[i]->NextTD = 0x1;

		/* Increase */
		Pool += sizeof(OhciGTransferDescriptor_t);
		PoolPhys += sizeof(OhciGTransferDescriptor_t);
		BufAddr += 0x200;
	}

	/* Setup Interrupt Table 
	 * We simply use the DMA
	 * allocation */
	Controller->IntrTable = (OhciIntrTable_t*)(Controller->HccaSpace + 512);

	/* Setup first level */
	EdLevel = Controller->HccaSpace + 512;
	EdLevel += 16 * sizeof(OhciEndpointDescriptor_t);
	for (i = 0; i < 16; i++)
	{
		Controller->IntrTable->Ms16[i].NextED = EdLevel + ((i / 2) * sizeof(OhciEndpointDescriptor_t));
		Controller->IntrTable->Ms16[i].NextEDVirtual = EdLevel + ((i / 2) * sizeof(OhciEndpointDescriptor_t));
		Controller->IntrTable->Ms16[i].Flags = X86_OHCI_EP_SKIP;
	}

	/* Second level (8 ms) */
	EdLevel += 8 * sizeof(OhciEndpointDescriptor_t);
	for (i = 0; i < 8; i++)
	{
		Controller->IntrTable->Ms8[i].NextED = EdLevel + ((i / 2) * sizeof(OhciEndpointDescriptor_t));
		Controller->IntrTable->Ms8[i].NextEDVirtual = EdLevel + ((i / 2) * sizeof(OhciEndpointDescriptor_t));
		Controller->IntrTable->Ms8[i].Flags = X86_OHCI_EP_SKIP;
	}

	/* Third level (4 ms) */
	EdLevel += 4 * sizeof(OhciEndpointDescriptor_t);
	for (i = 0; i < 4; i++)
	{
		Controller->IntrTable->Ms4[i].NextED = EdLevel + ((i / 2) * sizeof(OhciEndpointDescriptor_t));
		Controller->IntrTable->Ms4[i].NextEDVirtual = EdLevel + ((i / 2) * sizeof(OhciEndpointDescriptor_t));
		Controller->IntrTable->Ms4[i].Flags = X86_OHCI_EP_SKIP;
	}

	/* Fourth level (2 ms) */
	EdLevel += 2 * sizeof(OhciEndpointDescriptor_t);
	for (i = 0; i < 2; i++)
	{
		Controller->IntrTable->Ms2[i].NextED = EdLevel + sizeof(OhciEndpointDescriptor_t);
		Controller->IntrTable->Ms2[i].NextEDVirtual = EdLevel + sizeof(OhciEndpointDescriptor_t);
		Controller->IntrTable->Ms2[i].Flags = X86_OHCI_EP_SKIP;
	}

	/* Last level (1 ms) */
	Controller->IntrTable->Ms1[0].NextED = 0;
	Controller->IntrTable->Ms1[0].NextEDVirtual = 0;
	Controller->IntrTable->Ms1[0].Flags = X86_OHCI_EP_SKIP;

	/* Setup HCCA */
	for (i = 0; i < 32; i++)
	{
		/* 0 -> 0     16 -> 0
		 * 1 -> 8     17 -> 8
		 * 2 -> 4     18 -> 4
		 * 3 -> 12    19 -> 12
		 * 4 -> 2     20 -> 2
		 * 5 -> 10    21 -> 10
		 * 6 -> 6     22 -> 6
		 * 7 -> 14    23 -> 14
		 * 8 -> 1     24 -> 1
		 * 9 -> 9     25 -> 9
		 * 10 -> 5    26 -> 5
		 * 11 -> 13   27 -> 13
		 * 12 -> 3    28 -> 3
		 * 13 -> 11   29 -> 11
		 * 14 -> 7    30 -> 7
		 * 15 -> 15   31 -> 15 */
		Controller->ED32[i] = (OhciEndpointDescriptor_t*)
			((Controller->HccaSpace + 512) + (_Balance[i & 0xF] * sizeof(OhciEndpointDescriptor_t)));
		Controller->HCCA->InterruptTable[i] = 
			((Controller->HccaSpace + 512) + (_Balance[i & 0xF] * sizeof(OhciEndpointDescriptor_t)));
		
		/* This gives us the tree 
		 * This means our 16 first ED's in the IntrTable are the buttom of the tree 
		 *   0          1         2         3        4         5         6        7        8        9        10        11        12        13        15       
		 *  / \        / \       / \       / \      / \       / \       / \      / \      / \      / \       / \       / \       / \       / \       / \ 
		 * 0  16      8  24     4  20     12 28    2  18     10 26     6  22    14 30    1  17    9  25     5  21     13 29     3  19     7  23     15 31
		 */

	}

	/* Load Balancing */
	Controller->I32 = 0;
	Controller->I16 = 0;
	Controller->I8 = 0;
	Controller->I4 = 0;
	Controller->I2 = 0;

	/* Allocate a Transaction list */
	Controller->TransactionsWaitingBulk = 0;
	Controller->TransactionsWaitingControl = 0;
	Controller->TransactionQueueBulk = 0;
	Controller->TransactionQueueControl = 0;
	Controller->TransactionList = list_create(LIST_SAFE);
}

/* Take control of OHCI controller */
int OhciTakeControl(OhciController_t *Controller)
{
	/* Hold stuff */
	uint32_t Temp, i;

	/* Is SMM the bitch? */
	if (Controller->Registers->HcControl & X86_OHCI_CTRL_INT_ROUTING)
	{
		/* Ok, SMM has control, now give me my hat back */
		Temp = Controller->Registers->HcCommandStatus;
		Temp |= X86_OHCI_CMD_OWNERSHIP;
		Controller->Registers->HcCommandStatus = Temp;

		/* Wait for InterruptRouting to clear */
		WaitForConditionWithFault(i, (Controller->Registers->HcControl & X86_OHCI_CTRL_INT_ROUTING) == 0, 250, 10);
		
		if (i != 0)
		{
			/* Did not work, reset bit, try that */
			Controller->Registers->HcControl &= ~X86_OHCI_CTRL_INT_ROUTING;
			WaitForConditionWithFault(i, (Controller->Registers->HcControl & X86_OHCI_CTRL_INT_ROUTING) == 0, 250, 10);

			if (i != 0)
			{
				printf("USB_OHCI: failed to clear routing bit\n");
				printf("USB_OHCI: SMM Won't give us the Controller, we're backing down >(\n");
				MmPhysicalFreeBlock(Controller->HccaSpace);
				kfree(Controller);
				return -1;
			}
		}
	}
	/* Is BIOS the bitch?? */
	else if (Controller->Registers->HcControl & X86_OHCI_CTRL_FSTATE_BITS)
	{
		if ((Controller->Registers->HcControl & X86_OHCI_CTRL_FSTATE_BITS) != X86_OHCI_CTRL_USB_WORKING)
		{
			/* Resume Usb Operations */
			OhciSetMode(Controller, X86_OHCI_CTRL_USB_WORKING);

			/* Wait 10 ms */
			StallMs(10);
		}
	}
	else
	{
		/* Cold Boot */

		/* Wait 10 ms */
		StallMs(10);
	}

	return 0;
}

/* Resets the controllre to a working state from initial */
void OhciSetup(OhciController_t *Controller)
{
	UsbHc_t *HcCtrl;
	uint32_t TempValue = 0, Temp = 0, FmInt = 0;
	int i;

	/* Step 1. Verify the Revision */
	TempValue = (Controller->Registers->HcRevision & 0xFF);
	if (TempValue != X86_OHCI_REVISION
		&& TempValue != 0x11)
	{
		printf("OHCI Revision is wrong (0x%x), exiting :(\n", TempValue);
		MmPhysicalFreeBlock(Controller->HccaSpace);
		kfree(Controller);
		return;
	}

	/* Step 2. Init Virtual Queues */
	OhciInitQueues(Controller);

	/* Step 3. Gain control of Controller */
	if (OhciTakeControl(Controller) == -1)
		return;
	
	/* Step 4. Verify HcFmInterval and save it  */
	FmInt = Controller->Registers->HcFmInterval;

	/* Sanity */
	if (X86_OHCI_GETFSMP(FmInt) == 0)
	{
		/* What the fuck OHCI */
		FmInt |= X86_OHCI_FSMP(FmInt) << 16;
	}

	if ((FmInt & X86_OHCI_FI_MASK) == 0)
	{
		/* Really, BIOS?! */
		FmInt |= X86_OHCI_FI;
	}

	/* We should check here if HcControl has RemoteWakeup Connected and then set device to remote wake capable */

	/* Disable All Interrupts */
	Controller->Registers->HcInterruptDisable = (uint32_t)X86_OHCI_INTR_MASTER_INTR;

	/* Perform a reset of HcCtrl Controller */
	OhciSetMode(Controller, X86_OHCI_CTRL_USB_SUSPEND);
	StallMs(10);
	
	/* Set bit 0 to Request reboot */
	Temp = Controller->Registers->HcCommandStatus;
	Temp |= X86_OHCI_CMD_RESETCTRL;
	Controller->Registers->HcCommandStatus = Temp;

	/* Wait for reboot (takes maximum of 10 ms) */
	WaitForConditionWithFault(i, (Controller->Registers->HcCommandStatus & X86_OHCI_CMD_RESETCTRL) == 0, 50, 1);

	/* Sanity */
	if (i != 0)
	{
		printf("USB_OHCI: controller %u failed to reboot\n", Controller->HcdId);
		printf("USB_OHCI: Reset Timeout :(\n");
		return;
	}

	/* Restore FmInt */
	Controller->Registers->HcFmInterval = FmInt;

	/* Toggle FIT */
	Controller->Registers->HcFmInterval ^= 0x80000000;

	/**************************************/
	/* We now have 2 ms to complete setup
	 * and put it in Operational Mode */
	/**************************************/

	/* Set HcHCCA to phys Address of HCCA */
	Controller->Registers->HcHCCA = Controller->HccaSpace;

	/* Initial values for frame */
	Controller->HCCA->CurrentFrame = 0;
	Controller->HCCA->HeadDone = 0;

	/* Setup initial ED points */
	Controller->Registers->HcControlHeadED =
		Controller->Registers->HcControlCurrentED = 0;
	Controller->Registers->HcBulkHeadED =
		Controller->Registers->HcBulkCurrentED = 0;

	/* Set HcEnableInterrupt to all except SOF and OC */
	Controller->Registers->HcInterruptDisable = (X86_OHCI_INTR_SOF | X86_OHCI_INTR_ROOT_HUB_EVENT | X86_OHCI_INTR_OWNERSHIP_EVENT);
	Controller->Registers->HcInterruptStatus = ~(uint32_t)0;
	Controller->Registers->HcInterruptEnable = (X86_OHCI_INTR_SCHEDULING_OVRRN | X86_OHCI_INTR_HEAD_DONE |
		X86_OHCI_INTR_RESUME_DETECT | X86_OHCI_INTR_FATAL_ERROR | X86_OHCI_INTR_FRAME_OVERFLOW | X86_OHCI_INTR_MASTER_INTR);

	/* Set HcPeriodicStart to a value that is 90% of FrameInterval in HcFmInterval */
	TempValue = (FmInt & X86_OHCI_FI_MASK);
	Controller->Registers->HcPeriodicStart = (TempValue / 10) * 9;

	/* Clear Lists, Mode, Ratio and IR */
	Temp = (Temp & ~(0x0000003C | X86_OHCI_CTRL_USB_SUSPEND | 0x3 | 0x100));

	/* Set Ratio (4:1) and Mode (Operational) */
	Temp |= (0x3 | X86_OHCI_CTRL_USB_WORKING);
	Controller->Registers->HcControl = Temp | X86_OHCI_CTRL_ALL_LISTS | X86_OHCI_CTRL_REMOTE_WAKE;

	/* Controller is now running! */
	printf("OHCI: Controller %u Started, Control 0x%x, Ints 0x%x, FmInterval 0x%x\n",
		Controller->Id, Controller->Registers->HcControl, Controller->Registers->HcInterruptEnable, Controller->Registers->HcFmInterval);

	/* Check Power Mode */
	if (Controller->Registers->HcRhDescriptorA & (1 << 9))
	{
		Controller->PowerMode = X86_OHCI_POWER_ALWAYS_ON;
		Controller->Registers->HcRhStatus = X86_OHCI_STATUS_POWER_ON;
		Controller->Registers->HcRhDescriptorB = 0;
	}
	else
	{
		/* Ports are power-switched 
		 * Check Mode */
		if (Controller->Registers->HcRhDescriptorA & (1 << 8))
		{
			/* This is favorable Mode 
			 * (If this is supported we set power-mask so that all Ports control their own power) */
			Controller->PowerMode = X86_OHCI_POWER_PORT_CONTROLLED;
			Controller->Registers->HcRhDescriptorB = 0xFFFF0000;
		}
		else
		{
			/* Global Power Switch */
			Controller->Registers->HcRhDescriptorB = 0;
			Controller->Registers->HcRhStatus = X86_OHCI_STATUS_POWER_ON;
			Controller->PowerMode = X86_OHCI_POWER_PORT_GLOBAL;
		}
	}

	/* Get Port count from (DescriptorA & 0x7F) */
	Controller->Ports = Controller->Registers->HcRhDescriptorA & 0x7F;

	/* Sanity */
	if (Controller->Ports > 15)
		Controller->Ports = 15;

	/* Set RhA */
	Controller->Registers->HcRhDescriptorA &= ~(0x00000000 | X86_OHCI_DESCA_DEVICE_TYPE);

	/* Get Power On Delay 
	 * PowerOnToPowerGoodTime (24 - 31)
	 * This byte specifies the duration HCD has to wait before 
	 * accessing a powered-on Port of the Root Hub. 
	 * It is implementation-specific.  The unit of time is 2 ms.  
	 * The duration is calculated as POTPGT * 2 ms.
	 */
	TempValue = Controller->Registers->HcRhDescriptorA;
	TempValue >>= 24;
	TempValue &= 0x000000FF;
	TempValue *= 2;

	/* Give it atleast 100 ms :p */
	if (TempValue < 100)
		TempValue = 100;

	Controller->PowerOnDelayMs = TempValue;

	printf("OHCI: Ports %u (power Mode %u, power delay %u)\n", 
		Controller->Ports, Controller->PowerMode, TempValue);

	/* Setup HCD */
	HcCtrl = UsbInitController((void*)Controller, X86_USB_TYPE_OHCI, Controller->Ports);

	/* Port Functions */
	HcCtrl->RootHubCheck = OhciPortsCheck;
	HcCtrl->PortSetup = OhciPortStatus;

	/* Transaction Functions */
	HcCtrl->TransactionInit = OhciTransactionInit;
	HcCtrl->TransactionSetup = OhciTransactionSetup;
	HcCtrl->TransactionIn = OhciTransactionIn;
	HcCtrl->TransactionOut = OhciTransactionOut;
	HcCtrl->TransactionSend = OhciTransactionSend;
	HcCtrl->InstallInterrupt = OhciInstallInterrupt;

	Controller->HcdId = UsbRegisterController(HcCtrl);

	/* Setup Ports */
	for (i = 0; i < (int)Controller->Ports; i++)
	{
		int p = i;

		/* Make sure power is on */
		if (!(Controller->Registers->HcRhPortStatus[i] & X86_OHCI_PORT_POWER_ENABLE))
		{
			/* Powerup! */
			Controller->Registers->HcRhPortStatus[i] = X86_OHCI_PORT_POWER_ENABLE;
		}

		/* Check if Port is connected */
		if (Controller->Registers->HcRhPortStatus[i] & X86_OHCI_PORT_CONNECTED)
			UsbEventCreate(UsbGetHcd(Controller->HcdId), p, X86_USB_EVENT_CONNECTED);
	}

	/* Now we can enable hub events (and clear interrupts) */
	Controller->Registers->HcInterruptStatus &= ~(uint32_t)0;
	Controller->Registers->HcInterruptEnable = X86_OHCI_INTR_ROOT_HUB_EVENT;
}

/* Reset Controller */
void OhciReset(OhciController_t *Controller)
{
	uint32_t TempValue, Temp, FmInt;
	int i;

	/* Disable All Interrupts */
	Controller->Registers->HcInterruptDisable = (uint32_t)X86_OHCI_INTR_MASTER_INTR;

	/* Perform a reset of HcCtrl Controller */
	OhciSetMode(Controller, X86_OHCI_CTRL_USB_SUSPEND);
	StallMs(200);

	/* Okiiii, reset Controller, we need to save FmInterval */
	FmInt = Controller->Registers->HcFmInterval;

	/* Set bit 0 to Request reboot */
	Temp = Controller->Registers->HcCommandStatus;
	Temp |= X86_OHCI_CMD_RESETCTRL;
	Controller->Registers->HcCommandStatus = Temp;

	/* Wait for reboot (takes maximum of 10 ms) */
	i = 0;
	while ((i < 500) && Controller->Registers->HcCommandStatus & X86_OHCI_CMD_RESETCTRL)
	{
		StallMs(1);
		i++;
	}

	/* Sanity */
	if (i == 500)
	{
		printf("OHCI: Reset Timeout :(\n");
		return;
	}

	/**************************************/
	/* We now have 2 ms to complete setup */
	/**************************************/

	/* Set HcHCCA to phys Address of HCCA */
	Controller->Registers->HcHCCA = Controller->HccaSpace;

	/* Initial values for frame */
	Controller->HCCA->CurrentFrame = 0;
	Controller->HCCA->HeadDone = 0;

	/* Setup initial ED points */
	Controller->Registers->HcControlHeadED =
		Controller->Registers->HcControlCurrentED = 0;
	Controller->Registers->HcBulkHeadED =
		Controller->Registers->HcBulkCurrentED = 0;

	/* Set HcEnableInterrupt to all except SOF and OC */
	Controller->Registers->HcInterruptDisable = (X86_OHCI_INTR_SOF | X86_OHCI_INTR_ROOT_HUB_EVENT | X86_OHCI_INTR_OWNERSHIP_EVENT);
	Controller->Registers->HcInterruptStatus = ~(uint32_t)0;
	Controller->Registers->HcInterruptEnable = (X86_OHCI_INTR_SCHEDULING_OVRRN | X86_OHCI_INTR_HEAD_DONE |
		X86_OHCI_INTR_RESUME_DETECT | X86_OHCI_INTR_FATAL_ERROR | X86_OHCI_INTR_FRAME_OVERFLOW | X86_OHCI_INTR_MASTER_INTR);

	/* Set HcPeriodicStart to a value that is 90% of FrameInterval in HcFmInterval */
	TempValue = (Controller->Registers->HcFmInterval & 0x3FFF);
	Controller->Registers->HcPeriodicStart = (TempValue / 10) * 9;

	/* Setup Control */
	Temp = Controller->Registers->HcControl;
	if (Temp & X86_OHCI_CTRL_REMOTE_WAKE)
		Temp |= X86_OHCI_CTRL_REMOTE_WAKE;

	/* Clear Lists, Mode, Ratio and IR */
	Temp = (Temp & ~(0x0000003C | X86_OHCI_CTRL_USB_SUSPEND | 0x3 | 0x100));

	/* Set Ratio (4:1) and Mode (Operational) */
	Temp |= (0x3 | X86_OHCI_CTRL_USB_WORKING);
	Controller->Registers->HcControl = Temp;

	/* Now restore FmInterval */
	Controller->Registers->HcFmInterval = FmInt;

	/* Controller is now running! */
	printf("OHCI: Controller %u Started, Control 0x%x\n",
		Controller->Id, Controller->Registers->HcControl);

	/* Check Power Mode */
	if (Controller->Registers->HcRhDescriptorA & (1 << 9))
	{
		Controller->PowerMode = X86_OHCI_POWER_ALWAYS_ON;
		Controller->Registers->HcRhStatus = X86_OHCI_STATUS_POWER_ON;
		Controller->Registers->HcRhDescriptorB = 0;
	}
	else
	{
		/* Ports are power-switched
		* Check Mode */
		if (Controller->Registers->HcRhDescriptorA & (1 << 8))
		{
			/* This is favorable Mode
			* (If this is supported we set power-mask so that all Ports control their own power) */
			Controller->PowerMode = X86_OHCI_POWER_PORT_CONTROLLED;
			Controller->Registers->HcRhDescriptorB = 0xFFFF0000;
		}
		else
		{
			/* Global Power Switch */
			Controller->Registers->HcRhDescriptorB = 0;
			Controller->Registers->HcRhStatus = X86_OHCI_STATUS_POWER_ON;
			Controller->PowerMode = X86_OHCI_POWER_PORT_GLOBAL;
		}
	}

	/* Now we can enable hub events (and clear interrupts) */
	Controller->Registers->HcInterruptStatus &= ~(uint32_t)0;
	Controller->Registers->HcInterruptEnable = X86_OHCI_INTR_ROOT_HUB_EVENT;
}

/* ED Functions */
uint32_t OhciAllocateEp(OhciController_t *Controller, uint32_t Type)
{
	uint32_t cIndex = 0;

	/* Pick a QH */
	SpinlockAcquire(&Controller->Lock);

	/* Grap it, locked operation */
	if (Type == X86_USB_REQUEST_TYPE_CONTROL)
	{
		cIndex = Controller->EDIndexControl;
		Controller->EDIndexControl++;

		/* cIndex Sanity */
		if (Controller->EDIndexControl == (X86_OHCI_POOL_CONTROL_EDS - 1))
			Controller->EDIndexControl = 0;
	}
	else if (Type == X86_OHCI_INDEX_TYPE_INTERRUPT)
	{
		/* Allocate */
		Addr_t aSpace = (Addr_t)kmalloc(sizeof(OhciEndpointDescriptor_t) + X86_OHCI_STRUCT_ALIGN);
		cIndex = OhciAlign(aSpace, X86_OHCI_STRUCT_ALIGN_BITS, X86_OHCI_STRUCT_ALIGN);

		/* Zero it out */
		memset((void*)cIndex, 0, sizeof(OhciEndpointDescriptor_t));
	}
	else
	{
		cIndex = Controller->EDIndexBulk;
		Controller->EDIndexBulk++;

		/* cIndex Sanity */
		if (Controller->EDIndexBulk == X86_OHCI_POOL_BULK_EDS)
			Controller->EDIndexBulk = X86_OHCI_POOL_CONTROL_EDS;
	}

	/* Release Lock */
	SpinlockRelease(&Controller->Lock);

	return cIndex;
}

void OhciEpInit(OhciEndpointDescriptor_t *Ed, UsbHcTransaction_t *FirstTd, uint32_t Type,
	uint32_t Address, uint32_t Endpoint, uint32_t PacketSize, uint32_t LowSpeed)
{
	/* Set Head & Tail Td */
	if ((Addr_t)FirstTd == X86_OHCI_TRANSFER_END_OF_LIST)
	{
		Ed->HeadPtr = X86_OHCI_TRANSFER_END_OF_LIST;
		Ed->TailPtr = 0;
	}
	else
	{
		Addr_t FirstTdAddr = (Addr_t)FirstTd->TransferDescriptor;
		Addr_t LastTd = 0;

		/* Get tail */
		UsbHcTransaction_t *FirstLink = FirstTd;
		while (FirstLink->Link)
			FirstLink = FirstLink->Link;
		LastTd = (Addr_t)FirstLink->TransferDescriptor;

		Ed->TailPtr = MmVirtualGetMapping(NULL, (Addr_t)LastTd);
		Ed->HeadPtr = MmVirtualGetMapping(NULL, (Addr_t)FirstTdAddr) | 0x1;
	}
		
		
	/* Setup Flags 
	 * HighSpeed Bulk/Control/Interrupt */
	Ed->Flags = X86_OHCI_EP_SKIP;
	Ed->Flags |= (Address & X86_OHCI_EP_ADDR_BITS);
	Ed->Flags |= X86_OHCI_EP_EP_NUM((Endpoint & X86_OHCI_EP_EP_NUM_BITS));
	Ed->Flags |= X86_OHCI_EP_PID_TD; /* Get PID from Td */
	Ed->Flags |= X86_OHCI_EP_LOWSPEED(LowSpeed);
	Ed->Flags |= X86_OHCI_EP_PACKET_SIZE((PacketSize & X86_OHCI_EP_PACKET_BITS));
	Ed->Flags |= X86_OHCI_EP_TYPE((Type & 0xF));
}

/* Td Functions */
uint32_t OhciAllocateTd(OhciController_t *Controller)
{
	int32_t cIndex = -1;
	OhciGTransferDescriptor_t *Td;

	/* Pick a QH */
	SpinlockAcquire(&Controller->Lock);

	/* Grap it, locked operation */
	while (cIndex == -1)
	{
		Td = Controller->TDPool[Controller->TDIndex];

		if (!(Td->Flags & X86_OHCI_TRANSFER_BUF_NOCC))
		{
			/* Done! */
			cIndex = Controller->TDIndex;
			Td->Flags |= X86_OHCI_TRANSFER_BUF_NOCC;
		}

		Controller->TDIndex++;

		/* cIndex Sanity */
		if (Controller->TDIndex == X86_OHCI_POOL_NUM_TD)
			Controller->TDIndex = 0;
	}

	/* Release Lock */
	SpinlockRelease(&Controller->Lock);

	return (uint32_t)cIndex;
}

OhciGTransferDescriptor_t *OhciTdSetup(OhciController_t *Controller, 
	Addr_t NextTD, uint32_t Toggle, uint8_t RequestDirection,
	uint8_t RequestType, uint8_t RequestValueLo, uint8_t RequestValueHi, uint16_t RequestIndex,
	uint16_t RequestLength, void **TDBuffer)
{
	UsbPacket_t *Packet;
	OhciGTransferDescriptor_t *Td;
	Addr_t TdPhys;
	void *Buffer;
	uint32_t TDIndex;

	/* Allocate a Td */
	TDIndex = OhciAllocateTd(Controller);

	/* Grab a Td and a Buffer */
	Td = Controller->TDPool[TDIndex];
	Buffer = Controller->TDPoolBuffers[TDIndex];
	TdPhys = Controller->TDPoolPhys[TDIndex];
	
	/* EOL ? */
	if (NextTD == X86_OHCI_TRANSFER_END_OF_LIST)
		Td->NextTD = X86_OHCI_TRANSFER_END_OF_LIST;
	else	/* Get physical Address of NextTD and set NextTD to that */
		Td->NextTD = MmVirtualGetMapping(NULL, (VirtAddr_t)NextTD); 

	/* Setup the Td for a SETUP Td */
	Td->Flags = 0;
	Td->Flags |= X86_OHCI_TRANSFER_BUF_ROUNDING;
	Td->Flags |= X86_OHCI_TRANSFER_BUF_PID_SETUP;
	Td->Flags |= X86_OHCI_TRANSFER_BUF_NO_INTERRUPT;
	Td->Flags |= (Toggle << 24);
	Td->Flags |= X86_OHCI_TRANSFER_BUF_TD_TOGGLE;
	Td->Flags |= X86_OHCI_TRANSFER_BUF_NOCC;

	/* Setup the SETUP Request */
	*TDBuffer = Buffer;
	Packet = (UsbPacket_t*)Buffer;
	Packet->Direction = RequestDirection;
	Packet->Type = RequestType;
	Packet->ValueLo = RequestValueLo;
	Packet->ValueHi = RequestValueHi;
	Packet->Index = RequestIndex;
	Packet->Length = RequestLength;

	/* Set Td Buffer */
	Td->Cbp = MmVirtualGetMapping(NULL, (VirtAddr_t)Buffer);
	Td->BufferEnd = Td->Cbp + sizeof(UsbPacket_t) - 1;

	return Td;
}

OhciGTransferDescriptor_t *OhciTdIo(OhciController_t *Controller,
	Addr_t NextTD, uint32_t Toggle, uint32_t pid, 
	uint32_t Length, void **TDBuffer)
{
	OhciGTransferDescriptor_t *Td;
	Addr_t TdPhys;
	void *Buffer;
	uint32_t TDIndex;

	/* Allocate a Td */
	TDIndex = OhciAllocateTd(Controller);

	/* Grab a Td and a Buffer */
	Td = Controller->TDPool[TDIndex];
	Buffer = Controller->TDPoolBuffers[TDIndex];
	TdPhys = Controller->TDPoolPhys[TDIndex];

	/* EOL ? */
	if (NextTD == X86_OHCI_TRANSFER_END_OF_LIST)
		Td->NextTD = X86_OHCI_TRANSFER_END_OF_LIST;
	else	/* Get physical Address of NextTD and set NextTD to that */
		Td->NextTD = MmVirtualGetMapping(NULL, (VirtAddr_t)NextTD);

	/* Setup the Td for a IO Td */
	Td->Flags = 0;
	Td->Flags |= X86_OHCI_TRANSFER_BUF_ROUNDING;
	Td->Flags |= pid;
	Td->Flags |= X86_OHCI_TRANSFER_BUF_NO_INTERRUPT;
	Td->Flags |= X86_OHCI_TRANSFER_BUF_TD_TOGGLE;
	Td->Flags |= X86_OHCI_TRANSFER_BUF_NOCC;
	Td->Flags |= (Toggle << 24);

	*TDBuffer = Buffer;
	
	/* Bytes to transfer?? */
	if (Length > 0)
	{
		Td->Cbp = MmVirtualGetMapping(NULL, (VirtAddr_t)Buffer);
		Td->BufferEnd = Td->Cbp + Length - 1;
	}
	else
	{
		Td->Cbp = 0;
		Td->BufferEnd = 0;
	}
		
	return Td;
}

/* Transaction Functions */

/* This one prepaires an ED */
void OhciTransactionInit(void *Controller, UsbHcRequest_t *Request)
{
	OhciController_t *Ctrl = (OhciController_t*)Controller;
	uint32_t Temp;

	Temp = OhciAllocateEp(Ctrl, Request->Type);
	Ctrl->EDPool[Temp]->Flags |= X86_OHCI_EP_SKIP;
	Request->Data = Ctrl->EDPool[Temp];

	/* Set as not Completed for start */
	Request->Completed = 0;
}

/* This one prepaires an setup Td */
UsbHcTransaction_t *OhciTransactionSetup(void *Controller, UsbHcRequest_t *Request)
{
	OhciController_t *Ctrl = (OhciController_t*)Controller;
	UsbHcTransaction_t *Transaction;

	/* Allocate Transaction */
	Transaction = (UsbHcTransaction_t*)kmalloc(sizeof(UsbHcTransaction_t));
	Transaction->IoBuffer = 0;
	Transaction->IoLength = 0;
	Transaction->Link = NULL;

	/* Create the Td */
	Transaction->TransferDescriptor = (void*)OhciTdSetup(Ctrl, X86_OHCI_TRANSFER_END_OF_LIST, 
		Request->Device->Endpoints[Request->Endpoint]->Toggle, Request->Packet.Direction, Request->Packet.Type,
		Request->Packet.ValueLo, Request->Packet.ValueHi, Request->Packet.Index, 
		Request->Packet.Length, &Transaction->TransferBuffer);

	/* If previous Transaction */
	if (Request->Transactions != NULL)
	{
		OhciGTransferDescriptor_t *PrevTd;
		UsbHcTransaction_t *cTrans = Request->Transactions;

		while (cTrans->Link)
			cTrans = cTrans->Link;
		
		PrevTd = (OhciGTransferDescriptor_t*)cTrans->TransferDescriptor;
		PrevTd->NextTD = MmVirtualGetMapping(NULL, (VirtAddr_t)Transaction->TransferDescriptor);
	}

	return Transaction;
}

/* This one prepaires an in Td */
UsbHcTransaction_t *OhciTransactionIn(void *Controller, UsbHcRequest_t *Request)
{
	OhciController_t *Ctrl = (OhciController_t*)Controller;
	UsbHcTransaction_t *Transaction;

	/* Allocate Transaction */
	Transaction = (UsbHcTransaction_t*)kmalloc(sizeof(UsbHcTransaction_t));
	Transaction->IoBuffer = Request->IoBuffer;
	Transaction->IoLength = Request->IoLength;
	Transaction->Link = NULL;

	/* Setup Td */
	Transaction->TransferDescriptor = (void*)OhciTdIo(Ctrl, X86_OHCI_TRANSFER_END_OF_LIST,
		Request->Device->Endpoints[Request->Endpoint]->Toggle, 
		X86_OHCI_TRANSFER_BUF_PID_IN, Request->IoLength,
		&Transaction->TransferBuffer);

	/* If previous Transaction */
	if (Request->Transactions != NULL)
	{
		OhciGTransferDescriptor_t *PrevTd;
		UsbHcTransaction_t *cTrans = Request->Transactions;

		while (cTrans->Link)
			cTrans = cTrans->Link;

		PrevTd = (OhciGTransferDescriptor_t*)cTrans->TransferDescriptor;
		PrevTd->NextTD = MmVirtualGetMapping(NULL, (VirtAddr_t)Transaction->TransferDescriptor);
	}

	return Transaction;
}

/* This one prepaires an out Td */
UsbHcTransaction_t *OhciTransactionOut(void *Controller, UsbHcRequest_t *Request)
{
	OhciController_t *Ctrl = (OhciController_t*)Controller;
	UsbHcTransaction_t *Transaction;

	/* Allocate Transaction */
	Transaction = (UsbHcTransaction_t*)kmalloc(sizeof(UsbHcTransaction_t));
	Transaction->IoBuffer = 0;
	Transaction->IoLength = 0;
	Transaction->Link = NULL;

	/* Setup Td */
	Transaction->TransferDescriptor = (void*)OhciTdIo(Ctrl, X86_OHCI_TRANSFER_END_OF_LIST,
		Request->Device->Endpoints[Request->Endpoint]->Toggle, 
		X86_OHCI_TRANSFER_BUF_PID_OUT, Request->IoLength,
		&Transaction->TransferBuffer);

	/* Copy Data */
	if (Request->IoBuffer != NULL && Request->IoLength != 0)
		memcpy(Transaction->TransferBuffer, Request->IoBuffer, Request->IoLength);

	/* If previous Transaction */
	if (Request->Transactions != NULL)
	{
		OhciGTransferDescriptor_t *PrevTd;
		UsbHcTransaction_t *cTrans = Request->Transactions;

		while (cTrans->Link)
			cTrans = cTrans->Link;

		PrevTd = (OhciGTransferDescriptor_t*)cTrans->TransferDescriptor;
		PrevTd->NextTD = MmVirtualGetMapping(NULL, (VirtAddr_t)Transaction->TransferDescriptor);
	}

	return Transaction;
}

/* This one queues the Transaction up for processing */
void OhciTransactionSend(void *Controller, UsbHcRequest_t *Request)
{
	/* Wuhu */
	UsbHcTransaction_t *Transaction = Request->Transactions;
	OhciController_t *Ctrl = (OhciController_t*)Controller;
	int Completed = 1;
	OhciGTransferDescriptor_t *Td = NULL;
	uint32_t CondCode;
	Addr_t EdAddress;

	/* Get physical */
	EdAddress = MmVirtualGetMapping(NULL, (VirtAddr_t)Request->Data);

	/* Set as not Completed for start */
	Request->Completed = 0;

	/* Add dummy Td to end */
	UsbTransactionOut(UsbGetHcd(Ctrl->HcdId), Request, 1, NULL, 0);

	/* Iterate and set last to INT */
	Transaction = Request->Transactions;
	while (Transaction->Link->Link)
	{
#ifdef _OHCI_DIAGNOSTICS_
		printf("Td (Addr 0x%x) Flags 0x%x, Cbp 0x%x, BufferEnd 0x%x, Next Td 0x%x\n", MmVirtualGetMapping(NULL, (VirtAddr_t)Td), Td->Flags, Td->Cbp, Td->BufferEnd, Td->NextTD);
#endif

		/* Next */
		Transaction = Transaction->Link;
	}

	/* Retrieve Td */
#ifndef _OHCI_DIAGNOSTICS_
	Td = (OhciGTransferDescriptor_t*)Transaction->TransferDescriptor;
	Td->Flags &= ~(X86_OHCI_TRANSFER_BUF_NO_INTERRUPT);
#endif
	
	/* Setup an ED for this */
	((OhciEndpointDescriptor_t*)Request->Data)->HcdData = (uint32_t)Request->Transactions;
	OhciEpInit(Request->Data, Request->Transactions, Request->Type,
		Request->Device->Address, Request->Endpoint, Request->Length, Request->LowSpeed);

	/* Now lets try the Transaction */
	SpinlockAcquire(&Ctrl->Lock);

	/* Set true */
	Completed = 1;

	/* Add this Transaction to list */
	list_append(Ctrl->TransactionList, list_create_node(0, Request->Data));

	/* Remove Skip */
	((OhciEndpointDescriptor_t*)Request->Data)->Flags &= ~(X86_OHCI_EP_SKIP);
	((OhciEndpointDescriptor_t*)Request->Data)->HeadPtr &= ~(0x1);

#ifdef _OHCI_DIAGNOSTICS_
	printf("Ed Address 0x%x, Flags 0x%x, Ed Tail 0x%x, Ed Head 0x%x, Next 0x%x\n", EdAddress, ((OhciEndpointDescriptor_t*)Request->Data)->Flags,
		((OhciEndpointDescriptor_t*)Request->Data)->TailPtr, ((OhciEndpointDescriptor_t*)Request->Data)->HeadPtr, ((OhciEndpointDescriptor_t*)Request->Data)->NextED);
#endif

	/* Add them to list */
	if (Request->Type == X86_USB_REQUEST_TYPE_CONTROL)
	{
		if (Ctrl->TransactionsWaitingControl > 0)
		{
			/* Insert it */
			if (Ctrl->TransactionQueueControl == 0)
				Ctrl->TransactionQueueControl = (uint32_t)Request->Data;
			else
			{
				OhciEndpointDescriptor_t *Ed = (OhciEndpointDescriptor_t*)Ctrl->TransactionQueueControl;

				/* Find tail */
				while (Ed->NextED)
					Ed = (OhciEndpointDescriptor_t*)Ed->NextEDVirtual;

				/* Insert it */
				Ed->NextED = EdAddress;
				Ed->NextEDVirtual = (Addr_t)Request->Data;
			}

			/* Increase */
			Ctrl->TransactionsWaitingControl++;

			/* Release spinlock */
			SpinlockRelease(&Ctrl->Lock);
		}
		else
		{
			/* Add it HcControl/BulkCurrentED */
			Ctrl->Registers->HcControlHeadED =
				Ctrl->Registers->HcControlCurrentED = EdAddress;

			/* Increase */
			Ctrl->TransactionsWaitingControl++;

			/* Release spinlock */
			SpinlockRelease(&Ctrl->Lock);

			/* Set Lists Filled (Enable Them) */
			Ctrl->Registers->HcCommandStatus |= X86_OHCI_CMD_TDACTIVE_CTRL;
		}
	}
	else if (Request->Type == X86_USB_REQUEST_TYPE_BULK)
	{
		if (Ctrl->TransactionsWaitingBulk > 0)
		{
			/* Insert it */
			if (Ctrl->TransactionQueueBulk == 0)
				Ctrl->TransactionQueueBulk = (Addr_t)Request->Data;
			else
			{
				OhciEndpointDescriptor_t *Ed = (OhciEndpointDescriptor_t*)Ctrl->TransactionQueueBulk;

				/* Find tail */
				while (Ed->NextED)
					Ed = (OhciEndpointDescriptor_t*)Ed->NextEDVirtual;

				/* Insert it */
				Ed->NextED = EdAddress;
				Ed->NextEDVirtual = (uint32_t)Request->Data;
			}

			/* Increase */
			Ctrl->TransactionsWaitingBulk++;

			/* Release spinlock */
			SpinlockRelease(&Ctrl->Lock);
		}
		else
		{
			/* Add it HcControl/BulkCurrentED */
			Ctrl->Registers->HcBulkHeadED =
				Ctrl->Registers->HcBulkCurrentED = EdAddress;

			/* Increase */
			Ctrl->TransactionsWaitingBulk++;

			/* Release spinlock */
			SpinlockRelease(&Ctrl->Lock);

			/* Set Lists Filled (Enable Them) */
			Ctrl->Registers->HcCommandStatus |= X86_OHCI_CMD_TDACTIVE_BULK;
		}
	}
	
	/* Wait for interrupt */
#ifdef _OHCI_DIAGNOSTICS_
	printf("Current Frame 0x%x (HCCA), HcFrameNumber 0x%x, HcFrameInterval 0x%x, HcFrameRemaining 0x%x\n", Ctrl->HCCA->CurrentFrame, 
		Ctrl->Registers->HcFmNumber, Ctrl->Registers->HcFmInterval, Ctrl->Registers->HcFmRemaining);
	printf("Transaction sent.. waiting for reply..\n");
	StallMs(5000);
	printf("Current Frame 0x%x (HCCA), HcFrameNumber 0x%x, HcFrameInterval 0x%x, HcFrameRemaining 0x%x\n", Ctrl->HCCA->CurrentFrame,
		Ctrl->Registers->HcFmNumber, Ctrl->Registers->HcFmInterval, Ctrl->Registers->HcFmRemaining);
	printf("1. Current Control: 0x%x, Current Head: 0x%x\n",
		Ctrl->Registers->HcControlCurrentED, Ctrl->Registers->HcControlHeadED);
	StallMs(5000);
	printf("Current Frame 0x%x (HCCA), HcFrameNumber 0x%x, HcFrameInterval 0x%x, HcFrameRemaining 0x%x\n", Ctrl->HCCA->CurrentFrame,
		Ctrl->Registers->HcFmNumber, Ctrl->Registers->HcFmInterval, Ctrl->Registers->HcFmRemaining);
	printf("2. Current Control: 0x%x, Current Head: 0x%x\n",
		Ctrl->Registers->HcControlCurrentED, Ctrl->Registers->HcControlHeadED);
	printf("Current Control 0x%x, Current Cmd 0x%x\n",
		Ctrl->Registers->HcControl, Ctrl->Registers->HcCommandStatus);
#else
	SchedulerSleepThread((Addr_t*)Request->Data);
	_yield();
#endif


	/* Check Conditions (WithOUT dummy) */
#ifdef _OHCI_DIAGNOSTICS_
	printf("Ed Flags 0x%x, Ed Tail 0x%x, Ed Head 0x%x\n", ((OhciEndpointDescriptor_t*)Request->Data)->Flags,
		((OhciEndpointDescriptor_t*)Request->Data)->TailPtr, ((OhciEndpointDescriptor_t*)Request->Data)->HeadPtr);
#endif
	Transaction = Request->Transactions;
	while (Transaction->Link)
	{
		Td = (OhciGTransferDescriptor_t*)Transaction->TransferDescriptor;
		CondCode = (Td->Flags & 0xF0000000) >> 28;
#ifdef _OHCI_DIAGNOSTICS_
		printf("Td Flags 0x%x, Cbp 0x%x, BufferEnd 0x%x, Td Condition Code %u (%s)\n", Td->Flags, Td->Cbp, Td->BufferEnd, CondCode, OhciErrorMessages[CondCode]);
#endif

		if (CondCode == 0 && Completed == 1)
			Completed = 1;
		else
			Completed = 0;

		Transaction = Transaction->Link;
	}

#ifdef _OHCI_DIAGNOSTICS_
	printf("HcDoneHead: 0x%x\n", Ctrl->HCCA->HeadDone);
	printf("Current Frame 0x%x (HCCA), HcFrameNumber 0x%x, HcFrameInterval 0x%x, HcFrameRemaining 0x%x\n", Ctrl->HCCA->CurrentFrame,
		Ctrl->Registers->HcFmNumber, Ctrl->Registers->HcFmInterval, Ctrl->Registers->HcFmRemaining);
#endif

	/* Lets see... */
	if (Completed)
	{
		/* Build Buffer */
		Transaction = Request->Transactions;

		while (Transaction->Link)
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

		/* Set as Completed */
		Request->Completed = 1;
	}

#ifdef _OHCI_DIAGNOSTICS_
	for (;;);
#endif
}

/* Install an Interrupt Endpoint */
void OhciInstallInterrupt(void *Controller, UsbHcDevice_t *Device, UsbHcEndpoint_t *Endpoint,
	void *InBuffer, size_t InBytes, void(*Callback)(void*, size_t), void *Arg)
{
	UsbHc_t *Hcd = Device->HcDriver;
	OhciController_t *Ctrl = (OhciController_t*)Controller;
	OhciEndpointDescriptor_t *Ed = NULL, *iep = NULL;
	OhciGTransferDescriptor_t *Td = NULL;
	void *TDBuffer = NULL;
	uint32_t period = 32;
	uint32_t i;
	uint32_t LowSpeed = (Hcd->Ports[Device->Port]->FullSpeed == 1) ? 0 : 1;
	OhciPeridoicCallback_t *cb_info = (OhciPeridoicCallback_t*)kmalloc(sizeof(OhciPeridoicCallback_t));

	/* Calculate period */
	for (; (period >= Endpoint->Interval) && (period > 0);)
		period >>= 1;

	/* Grab an Ed */
	Ed = (OhciEndpointDescriptor_t*)OhciAllocateEp(Ctrl, X86_OHCI_INDEX_TYPE_INTERRUPT);

	/* Get Td(s) */
	i = OhciAllocateTd(Ctrl);
	Td = Ctrl->TDPool[i];
	TDBuffer = Ctrl->TDPoolBuffers[i];

	/* Setup CB Information */
	cb_info->Buffer = InBuffer;
	cb_info->Bytes = InBytes;
	cb_info->Callback = Callback;
	cb_info->Args = Arg;
	cb_info->TDIndex = i;

	if (Endpoint->Bandwidth > 1)
	{
		/* Oh god support this :( */
	}

	/* Setup Td */
	Td->Flags = 0;
	Td->Flags |= X86_OHCI_TRANSFER_BUF_ROUNDING;
	Td->Flags |= X86_OHCI_TRANSFER_BUF_PID_IN;
	Td->Flags |= ((Endpoint->Toggle & 0x1) << 24);
	Td->Flags |= X86_OHCI_TRANSFER_BUF_TD_TOGGLE;
	Td->Flags |= X86_OHCI_TRANSFER_BUF_NOCC;

	Td->NextTD = 0x1;

	Td->Cbp = MmVirtualGetMapping(NULL, (VirtAddr_t)TDBuffer);
	Td->BufferEnd = Td->Cbp + 0x200 - 1;

	/* Setup Ed */
	Ed->HeadPtr = MmVirtualGetMapping(NULL, (VirtAddr_t)Td);
	Ed->TailPtr = 0;
	Ed->HcdData = (uint32_t)cb_info;

	Ed->Flags = (Device->Address & X86_OHCI_EP_ADDR_BITS); /* Device Address */
	Ed->Flags |= X86_OHCI_EP_EP_NUM((Endpoint->Address & X86_OHCI_EP_EP_NUM_BITS));
	Ed->Flags |= X86_OHCI_EP_LOWSPEED(LowSpeed); /* Device Speed */
	Ed->Flags |= X86_OHCI_EP_PACKET_SIZE((Endpoint->MaxPacketSize & X86_OHCI_EP_PACKET_BITS));
	Ed->Flags |= X86_OHCI_EP_TYPE(2);

	/* Add transfer */
	list_append(Ctrl->TransactionList, list_create_node(0, Ed));

	/* Ok, queue it
	 * Lock & stop ints */
	SpinlockAcquire(&Ctrl->Lock);

	if (period == 1)
	{
		iep = &Ctrl->IntrTable->Ms1[0];

		/* Insert it */
		Ed->NextEDVirtual = iep->NextEDVirtual;
		Ed->NextED = iep->NextED;
		iep->NextED = MmVirtualGetMapping(NULL, (VirtAddr_t)Ed);
		iep->NextEDVirtual = (uint32_t)Ed;
	}
	else if (period == 2)
	{
		iep = &Ctrl->IntrTable->Ms2[Ctrl->I2];

		/* Insert it */
		Ed->NextEDVirtual = iep->NextEDVirtual;
		Ed->NextED = iep->NextED;
		iep->NextED = MmVirtualGetMapping(NULL, (VirtAddr_t)Ed);
		iep->NextEDVirtual = (uint32_t)Ed;

		/* Increase i2 */
		Ctrl->I2++;
		if (Ctrl->I2 == 2)
			Ctrl->I2 = 0;
	}
	else if (period == 4)
	{
		iep = &Ctrl->IntrTable->Ms4[Ctrl->I4];

		/* Insert it */
		Ed->NextEDVirtual = iep->NextEDVirtual;
		Ed->NextED = iep->NextED;
		iep->NextED = MmVirtualGetMapping(NULL, (VirtAddr_t)Ed);
		iep->NextEDVirtual = (uint32_t)Ed;

		/* Increase i4 */
		Ctrl->I4++;
		if (Ctrl->I4 == 4)
			Ctrl->I4 = 0;
	}
	else if (period == 8)
	{
		iep = &Ctrl->IntrTable->Ms8[Ctrl->I8];

		/* Insert it */
		Ed->NextEDVirtual = iep->NextEDVirtual;
		Ed->NextED = iep->NextED;
		iep->NextED = MmVirtualGetMapping(NULL, (VirtAddr_t)Ed);
		iep->NextEDVirtual = (uint32_t)Ed;

		/* Increase i8 */
		Ctrl->I8++;
		if (Ctrl->I8 == 8)
			Ctrl->I8 = 0;
	}
	else if (period == 16)
	{
		iep = &Ctrl->IntrTable->Ms16[Ctrl->I16];

		/* Insert it */
		Ed->NextEDVirtual = iep->NextEDVirtual;
		Ed->NextED = iep->NextED;
		iep->NextED = MmVirtualGetMapping(NULL, (VirtAddr_t)Ed);
		iep->NextEDVirtual = (uint32_t)Ed;

		/* Increase i16 */
		Ctrl->I16++;
		if (Ctrl->I16 == 16)
			Ctrl->I16 = 0;
	}
	else
	{
		/* 32 */
		iep = Ctrl->ED32[Ctrl->I32];

		/* Insert it */
		Ed->NextEDVirtual = (Addr_t)iep;
		Ed->NextED = MmVirtualGetMapping(NULL, (VirtAddr_t)iep);

		/* Make int-table point to this */
		Ctrl->HCCA->InterruptTable[Ctrl->I32] = MmVirtualGetMapping(NULL, (VirtAddr_t)Ed);
		Ctrl->ED32[Ctrl->I32] = Ed;

		/* Increase i32 */
		Ctrl->I32++;
		if (Ctrl->I32 == 32)
			Ctrl->I32 = 0;
	}

	/* Done */
	SpinlockRelease(&Ctrl->Lock);

	/* Enable Queue in case it was disabled */
	Ctrl->Registers->HcControl |= X86_OCHI_CTRL_PERIODIC_LIST;
}

/* Process Done Queue */
void OhciProcessDoneQueue(OhciController_t *Controller, Addr_t done_head)
{
	list_t *Transactions = Controller->TransactionList;
	list_node_t *ta = NULL;
	Addr_t td_physical;
	uint32_t transfer_type = 0;
	int n;

	/* Find it */
	n = 0;
	ta = list_get_node_by_id(Transactions, 0, n);
	while (ta != NULL)
	{
		OhciEndpointDescriptor_t *Ed = (OhciEndpointDescriptor_t*)ta->data;
		transfer_type = (Ed->Flags >> 27);

		/* Special Case */
		if (transfer_type == 2)
		{
			/* Interrupt */
			OhciPeridoicCallback_t *cb_info = (OhciPeridoicCallback_t*)Ed->HcdData;
			
			/* Is it this? */
			if (Controller->TDPoolPhys[cb_info->TDIndex] == done_head)
			{
				/* Yeps */
				void *TDBuffer = Controller->TDPoolBuffers[cb_info->TDIndex];
				OhciGTransferDescriptor_t *interrupt_td = Controller->TDPool[cb_info->TDIndex];
				uint32_t CondCode = (interrupt_td->Flags & 0xF0000000) >> 28;

				/* Sanity */
				if (CondCode == 0)
				{
					/* Get data */
					memcpy(cb_info->Buffer, TDBuffer, cb_info->Bytes);

					/* Inform Callback */
					cb_info->Callback(cb_info->Args, cb_info->Bytes);
				}

				/* Restart Td */
				interrupt_td->Cbp = interrupt_td->BufferEnd - 0x200 + 1;
				interrupt_td->Flags |= X86_OHCI_TRANSFER_BUF_NOCC;
				
				/* Toggle Td D bit!??!??! */

				/* Reset Ed */
				Ed->HeadPtr = Controller->TDPoolPhys[cb_info->TDIndex];

				/* We are done, return */
				return;
			}
		}
		else
		{
			UsbHcTransaction_t *t_list = (UsbHcTransaction_t*)Ed->HcdData;

			/* Process Td's and see if all transfers are done */
			while (t_list)
			{
				/* Get physical of Td */
				td_physical = MmVirtualGetMapping(NULL, (VirtAddr_t)t_list->TransferDescriptor);

				if (td_physical == done_head)
				{
					/* Is this the last? :> */
					if (t_list->Link == NULL || t_list->Link->Link == NULL)
					{
						n = 0xDEADBEEF;
						break;
					}
					else
					{
						/* Error :/ */
						OhciGTransferDescriptor_t *Td = (OhciGTransferDescriptor_t*)t_list->TransferDescriptor;
						uint32_t CondCode = (Td->Flags & 0xF0000000) >> 28;
						printf("FAILURE: Td Flags 0x%x, Td Condition Code %u (%s)\n", Td->Flags, CondCode, OhciErrorMessages[CondCode]);
						n = 0xBEEFDEAD;
						break;
					}
				}

				/* Next */
				t_list = t_list->Link;
			}

			if (n == 0xDEADBEEF || n == 0xBEEFDEAD)
				break;
		}

		n++;
		ta = list_get_node_by_id(Transactions, 0, n);	
	}

	if (ta != NULL && (n == 0xDEADBEEF || n == 0xBEEFDEAD))
	{
		/* Either it failed, or it succeded */

		/* So now, before waking up a sleeper we see if Transactions are pending
		 * if they are, we simply copy the queue over to the current */
		SpinlockAcquire(&Controller->Lock);

		/* Any Controls waiting? */
		if (transfer_type == 0)
		{
			if (Controller->TransactionsWaitingControl > 0)
			{
				/* Get physical of Ed */
				Addr_t ep_physical = MmVirtualGetMapping(NULL, Controller->TransactionQueueControl);

				/* Set it */
				Controller->Registers->HcControlHeadED =
					Controller->Registers->HcControlCurrentED = ep_physical;

				/* Start queue */
				Controller->Registers->HcCommandStatus |= X86_OHCI_CMD_TDACTIVE_CTRL;
				Controller->Registers->HcControl |= X86_OHCI_CTRL_CONTROL_LIST;
			}

			/* Reset control queue */
			Controller->TransactionQueueControl = 0;
			Controller->TransactionsWaitingControl = 0;
		}
		else if (transfer_type == 1)
		{
			/* Bulk */
			if (Controller->TransactionsWaitingBulk > 0)
			{
				/* Get physical of Ed */
				Addr_t ep_physical = MmVirtualGetMapping(NULL, Controller->TransactionQueueBulk);

				/* Add it to queue */
				Controller->Registers->HcBulkHeadED = 
					Controller->Registers->HcBulkCurrentED = ep_physical;

				/* Start queue */
				Controller->Registers->HcCommandStatus |= X86_OHCI_CMD_TDACTIVE_BULK;
				Controller->Registers->HcControl |= X86_OHCI_CTRL_BULK_LIST;
			}

			/* Reset control queue */
			Controller->TransactionQueueBulk = 0;
			Controller->TransactionsWaitingBulk = 0;
		}

		/* Done */
		SpinlockRelease(&Controller->Lock);

		/* Mark Ed Descriptor as sKip SEHR IMPORTANTE */
		((OhciEndpointDescriptor_t*)ta->data)->Flags = X86_OHCI_EP_SKIP;

		/* Wake a node */
		SchedulerWakeupOneThread((Addr_t*)ta->data);

		/* Remove from list */
		list_remove_by_node(Transactions, ta);

		/* Cleanup node */
		kfree(ta);
	}
}

/* Interrupt Handler
* Make sure that this Controller actually made the interrupt
* as this interrupt will be shared with other OHCI's */
int OhciInterruptHandler(void *data)
{
	uint32_t intr_state = 0;
	OhciController_t *Controller = (OhciController_t*)data;

	/* Is this our interrupt ? */
	if (Controller->HCCA->HeadDone != 0)
	{
		/* Acknowledge */
		intr_state = X86_OHCI_INTR_HEAD_DONE;

		if (Controller->HCCA->HeadDone & 0x1)
		{
			/* Get rest of interrupts, since head_done has halted */
			intr_state |= (Controller->Registers->HcInterruptStatus & Controller->Registers->HcInterruptEnable);
		}
	}
	else
	{
		/* Was it this Controller that made the interrupt?
		* We only want the interrupts we have set as enabled */
		intr_state = (Controller->Registers->HcInterruptStatus & Controller->Registers->HcInterruptEnable);

		if (intr_state == 0)
			return X86_IRQ_NOT_HANDLED;
	}

	/* Debug */
	//printf("OHCI: Controller %u Interrupt: 0x%x\n", Controller->HcdId, intr_state);

	/* Disable Interrupts */
	Controller->Registers->HcInterruptDisable = (uint32_t)X86_OHCI_INTR_MASTER_INTR;

	/* Fatal Error? */
	if (intr_state & X86_OHCI_INTR_FATAL_ERROR)
	{
		printf("OHCI %u: Fatal Error, resetting...\n", Controller->Id);
		OhciReset(Controller);
		return X86_IRQ_HANDLED;
	}

	/* Flag for end of frame Type interrupts */
	if (intr_state & (X86_OHCI_INTR_SCHEDULING_OVRRN | X86_OHCI_INTR_HEAD_DONE | X86_OHCI_INTR_SOF | X86_OHCI_INTR_FRAME_OVERFLOW))
		intr_state |= X86_OHCI_INTR_MASTER_INTR;

	/* Scheduling Overrun? */
	if (intr_state & X86_OHCI_INTR_SCHEDULING_OVRRN)
	{
		printf("OHCI %u: Scheduling Overrun\n", Controller->Id);

		/* Acknowledge Interrupt */
		Controller->Registers->HcInterruptStatus = X86_OHCI_INTR_SCHEDULING_OVRRN;
		intr_state = intr_state & ~(X86_OHCI_INTR_SCHEDULING_OVRRN);
	}

	/* Resume Detection? */
	if (intr_state & X86_OHCI_INTR_RESUME_DETECT)
	{
		printf("OHCI %u: Resume Detected\n", Controller->Id);

		/* We must wait 20 ms before putting Controller to Operational */
		DelayMs(20);
		OhciSetMode(Controller, X86_OHCI_CTRL_USB_WORKING);

		/* Acknowledge Interrupt */
		Controller->Registers->HcInterruptStatus = X86_OHCI_INTR_RESUME_DETECT;
		intr_state = intr_state & ~(X86_OHCI_INTR_RESUME_DETECT);
	}

	/* Frame Overflow
	* Happens when it rolls over from 0xFFFF to 0 */
	if (intr_state & X86_OHCI_INTR_FRAME_OVERFLOW)
	{
		/* Acknowledge Interrupt */
		Controller->Registers->HcInterruptStatus = X86_OHCI_INTR_FRAME_OVERFLOW;
		intr_state = intr_state & ~(X86_OHCI_INTR_FRAME_OVERFLOW);
	}

	/* Why yes, yes it was, wake up the Td handler thread
	* if it was head_done_writeback */
	if (intr_state & X86_OHCI_INTR_HEAD_DONE)
	{
		/* Wuhu, handle this! */
		uint32_t td_address = (Controller->HCCA->HeadDone & ~(0x00000001));

		OhciProcessDoneQueue(Controller, td_address);

		/* Acknowledge Interrupt */
		Controller->HCCA->HeadDone = 0;
		Controller->Registers->HcInterruptStatus = X86_OHCI_INTR_HEAD_DONE;
		intr_state = intr_state & ~(X86_OHCI_INTR_HEAD_DONE);
	}

	/* Root Hub Status Change
	 * Do a Port Status check */
	if (intr_state & X86_OHCI_INTR_ROOT_HUB_EVENT)
	{
		/* Port does not matter here */
		UsbEventCreate(UsbGetHcd(Controller->HcdId), 0, X86_USB_EVENT_ROOTHUB_CHECK);

		/* Acknowledge Interrupt */
		Controller->Registers->HcInterruptStatus = X86_OHCI_INTR_ROOT_HUB_EVENT;
		intr_state = intr_state & ~(X86_OHCI_INTR_ROOT_HUB_EVENT);
	}

	/* Start of Frame? */
	if (intr_state & X86_OHCI_INTR_SOF)
	{
		/* Acknowledge Interrupt */
		Controller->Registers->HcInterruptStatus = X86_OHCI_INTR_SOF;
		intr_state = intr_state & ~(X86_OHCI_INTR_SOF);
	}

	/* Mask out remaining interrupts, we dont use them */
	if (intr_state & ~(X86_OHCI_INTR_MASTER_INTR))
		Controller->Registers->HcInterruptDisable = intr_state;

	/* Enable Interrupts */
	Controller->Registers->HcInterruptEnable = (uint32_t)X86_OHCI_INTR_MASTER_INTR;

	return X86_IRQ_HANDLED;
}
