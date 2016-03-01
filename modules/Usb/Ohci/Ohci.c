/* MollenOS Ohci Module
*
* Copyright 2011 - 2016, Philip Meulengracht
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
* MollenOS USB OHCI Controller Driver
* Todo:
* Bandwidth scheduling
* Linux has a periodic timer event that checks if all finished td's has generated a interrupt to make sure
*/

/* Includes */
#include <Module.h>
#include "Ohci.h"

/* Additional Includes */
#include <UsbCore.h>
#include <Scheduler.h>
#include <Heap.h>
#include <Timers.h>
#include <Pci.h>

/* CLib */
#include <assert.h>
#include <string.h>

/* Globals */
uint32_t GlbOhciControllerId = 0;

/* Prototypes */
int OhciInterruptHandler(void *data);
void OhciReset(OhciController_t *Controller);
void OhciSetup(OhciController_t *Controller);

Addr_t OhciAllocateTd(OhciEndpoint_t *Ep, UsbTransferType_t Type);

/* Endpoint Prototypes */
void OhciEndpointSetup(void *Controller, UsbHcEndpoint_t *Endpoint);
void OhciEndpointDestroy(void *Controller, UsbHcEndpoint_t *Endpoint);

/* Transaction Prototypes */
void OhciTransactionInit(void *Controller, UsbHcRequest_t *Request);
UsbHcTransaction_t *OhciTransactionSetup(void *Controller, UsbHcRequest_t *Request);
UsbHcTransaction_t *OhciTransactionIn(void *Controller, UsbHcRequest_t *Request);
UsbHcTransaction_t *OhciTransactionOut(void *Controller, UsbHcRequest_t *Request);
void OhciTransactionSend(void *Controller, UsbHcRequest_t *Request);
void OhciTransactionDestroy(void *Controller, UsbHcRequest_t *Request);

/* Error Codes */
const char *GlbOhciDriverName = "MollenOS OHCI Driver";
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

/* Entry point of a module */
MODULES_API void ModuleInit(void *Data)
{
	/* Cast & Vars */
	MCoreDevice_t *mDevice = (MCoreDevice_t*)Data;
	OhciController_t *Controller = NULL;
	DeviceIoSpace_t *IoBase = NULL;
	uint16_t PciCommand;

	/* Allocate Resources for this Controller */
	Controller = (OhciController_t*)kmalloc(sizeof(OhciController_t));
	memset(Controller, 0, sizeof(OhciController_t));

	Controller->Device = mDevice;
	Controller->Id = GlbOhciControllerId;

	/* Get I/O Base */
	for (PciCommand = 0; PciCommand < DEVICEMANAGER_MAX_IOSPACES; PciCommand++) {
		if (mDevice->IoSpaces[PciCommand] != NULL
			&& mDevice->IoSpaces[PciCommand]->Type == DEVICE_IO_SPACE_MMIO) {
			IoBase = mDevice->IoSpaces[PciCommand];
			break;
		}
	}

	/* Sanity */
	if (IoBase == NULL)
	{
		/* Yea, give me my hat back */
		LogFatal("OHCI", "No memory space found for controller!");
		kfree(Controller);
		return;
	}

	/* Now we initialise */
	GlbOhciControllerId++;

	/* Memory map needed space */
	Controller->Registers = 
		(volatile OhciRegisters_t*)IoBase->VirtualBase;
	
	Controller->HccaSpace = AddressSpaceMap(AddressSpaceGetCurrent(), 
			0, PAGE_SIZE, ADDRESS_SPACE_FLAG_LOWMEM);
	Controller->HCCA = (volatile OhciHCCA_t*)Controller->HccaSpace;
	
	/* Reset Lock */
	SpinlockReset(&Controller->Lock);

	/* Memset HCCA Space */
	memset((void*)Controller->HCCA, 0, PAGE_SIZE);

	/* Allocate Irq */
	mDevice->IrqAvailable[0] = -1;
	mDevice->IrqHandler = OhciInterruptHandler;

	/* Register us for an irq */
	if (DmRequestResource(mDevice, ResourceIrq)) {
		
		/* Damnit! */
		LogFatal("OHCI", "Failed to allocate irq for use, bailing out!");
		kfree(Controller);
		return;
	}

	/* Enable memory and Bus mastering and clear interrupt disable */
	PciCommand = (uint16_t)PciDeviceRead(mDevice->BusDevice, 0x4, 2);
	PciDeviceWrite(mDevice->BusDevice, 0x4, (PciCommand & ~(0x400)) | 0x2 | 0x4, 2);

	/* Setup driver information */
	mDevice->Driver.Name = (char*)GlbOhciDriverName;
	mDevice->Driver.Data = Controller;
	mDevice->Driver.Version = 1;
	mDevice->Driver.Status = DriverActive;

	/* Debug */
#ifdef _OHCI_DIAGNOSTICS_
	printf("OHCI - Id %u, bar0: 0x%x (0x%x), dma: 0x%x\n",
		Controller->Id, Controller->ControlSpace,
		(Addr_t)Controller->Registers, Controller->HccaSpace);
#endif

	/* Reset Controller */
	OhciSetup(Controller);
}

/* Helpers */
void OhciSetMode(OhciController_t *Controller, uint32_t Mode)
{
	/* First we clear the current Operation Mode */
	uint32_t Val = Controller->Registers->HcControl;
	Val = (Val & ~OHCI_CONTROL_SUSPEND);
	Val |= Mode;
	Controller->Registers->HcControl = Val;
}

/* Aligns address (with roundup if alignment is set) */
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

/* This resets a Port */
void OhciPortReset(OhciController_t *Controller, size_t Port)
{
	/* Set reset */
	Controller->Registers->HcRhPortStatus[Port] = OHCI_PORT_RESET;

	/* Wait with timeout */
	WaitForCondition((Controller->Registers->HcRhPortStatus[Port] & OHCI_PORT_RESET) == 0,
		200, 10, "USB_OHCI: Failed to reset device on port %u\n", Port);

	/* Set Enable */
	if (Controller->PowerMode == OHCI_PWM_PORT_CONTROLLED)
		Controller->Registers->HcRhPortStatus[Port] = OHCI_PORT_ENABLED | OHCI_PORT_POWER;
	else
		Controller->Registers->HcRhPortStatus[Port] = OHCI_PORT_ENABLED;

	/* Delay before we make first transaction, it's important! */
	StallMs(50);
}

/* Callbacks */
void OhciPortStatus(void *ControllerData, UsbHcPort_t *Port)
{
	/* Cast Vars */
	OhciController_t *Controller = (OhciController_t*)ControllerData;
	uint32_t Status;

	/* Wait for power to stabilize */
	StallMs(Controller->PowerOnDelayMs);

	/* Reset Port */
	OhciPortReset(Controller, Port->Id);

	/* Update information in Port */
	Status = Controller->Registers->HcRhPortStatus[Port->Id];

	/* Is it connected? */
	if (Status & OHCI_PORT_CONNECTED)
		Port->Connected = 1;
	else
		Port->Connected = 0;

	/* Is it enabled? */
	if (Status & OHCI_PORT_ENABLED)
		Port->Enabled = 1;
	else
		Port->Enabled = 0;

	/* Is it full-speed? */
	if (Status & OHCI_PORT_LOW_SPEED)
		Port->Speed = LowSpeed;
	else
		Port->Speed = FullSpeed;

	/* Clear Connect Event */
	if (Controller->Registers->HcRhPortStatus[Port->Id] & OHCI_PORT_CONNECT_EVENT)
		Controller->Registers->HcRhPortStatus[Port->Id] = OHCI_PORT_CONNECT_EVENT;

	/* If Enable Event bit is set, clear it */
	if (Controller->Registers->HcRhPortStatus[Port->Id] & OHCI_PORT_ENABLE_EVENT)
		Controller->Registers->HcRhPortStatus[Port->Id] = OHCI_PORT_ENABLE_EVENT;

	/* If Suspend Event is set, clear it */
	if (Controller->Registers->HcRhPortStatus[Port->Id] & OHCI_PORT_SUSPEND_EVENT)
		Controller->Registers->HcRhPortStatus[Port->Id] = OHCI_PORT_SUSPEND_EVENT;

	/* If Over Current Event is set, clear it */
	if (Controller->Registers->HcRhPortStatus[Port->Id] & OHCI_PORT_OVR_CURRENT_EVENT)
		Controller->Registers->HcRhPortStatus[Port->Id] = OHCI_PORT_OVR_CURRENT_EVENT;

	/* If reset bit is set, clear it */
	if (Controller->Registers->HcRhPortStatus[Port->Id] & OHCI_PORT_RESET_EVENT)
		Controller->Registers->HcRhPortStatus[Port->Id] = OHCI_PORT_RESET_EVENT;
}

/* Port Functions */
void OhciPortCheck(OhciController_t *Controller, int Port)
{
	/* Vars */
	UsbHc_t *HcCtrl;

	/* Was it connect event and not disconnect ? */
	if (Controller->Registers->HcRhPortStatus[Port] & OHCI_PORT_CONNECT_EVENT)
	{
		/* Reset on Attach */
		OhciPortReset(Controller, Port);

		if (!(Controller->Registers->HcRhPortStatus[Port] & OHCI_PORT_CONNECTED))
		{
			/* Nah, disconnect event */
			/* Get HCD data */
			HcCtrl = UsbGetHcd(Controller->HcdId);

			/* Sanity */
			if (HcCtrl == NULL)
				return;

			/* Disconnect */
			UsbEventCreate(HcCtrl, Port, HcdDisconnectedEvent);
		}

		/* If Device is enabled, and powered, set it up */
		if ((Controller->Registers->HcRhPortStatus[Port] & OHCI_PORT_ENABLED)
			&& (Controller->Registers->HcRhPortStatus[Port] & OHCI_PORT_POWER))
		{
			/* Get HCD data */
			HcCtrl = UsbGetHcd(Controller->HcdId);

			/* Sanity */
			if (HcCtrl == NULL)
			{
				LogDebug("OHCI", "Controller %u is zombie and is trying to register Ports!!", Controller->Id);
				return;
			}

			/* Register Device */
			UsbEventCreate(HcCtrl, Port, HcdConnectedEvent);
		}
	}

	/* Clear Connect Event */
	if (Controller->Registers->HcRhPortStatus[Port] & OHCI_PORT_CONNECT_EVENT)
		Controller->Registers->HcRhPortStatus[Port] = OHCI_PORT_CONNECT_EVENT;

	/* If Enable Event bit is set, clear it */
	if (Controller->Registers->HcRhPortStatus[Port] & OHCI_PORT_ENABLE_EVENT)
		Controller->Registers->HcRhPortStatus[Port] = OHCI_PORT_ENABLE_EVENT;

	/* If Suspend Event is set, clear it */
	if (Controller->Registers->HcRhPortStatus[Port] & OHCI_PORT_SUSPEND_EVENT)
		Controller->Registers->HcRhPortStatus[Port] = OHCI_PORT_SUSPEND_EVENT;

	/* If Over Current Event is set, clear it */
	if (Controller->Registers->HcRhPortStatus[Port] & OHCI_PORT_OVR_CURRENT_EVENT)
		Controller->Registers->HcRhPortStatus[Port] = OHCI_PORT_OVR_CURRENT_EVENT;

	/* If reset bit is set, clear it */
	if (Controller->Registers->HcRhPortStatus[Port] & OHCI_PORT_RESET_EVENT)
		Controller->Registers->HcRhPortStatus[Port] = OHCI_PORT_RESET_EVENT;
}

/* Port Check */
void OhciPortsCheck(void *CtrlData)
{
	/* Vars & Cast */
	OhciController_t *Controller = (OhciController_t*)CtrlData;
	uint32_t i;

	/* Go through Ports */
	for (i = 0; i < Controller->Ports; i++)
	{
		/* Check íf Port has connected */
		OhciPortCheck(Controller, i);
	}
}

/* Initializes Controller Queues */
void OhciInitQueues(OhciController_t *Controller)
{
	/* Vars */
	Addr_t EdLevel;
	int i;

	/* Create the NULL Td */
	Controller->NullTd = (OhciGTransferDescriptor_t*)
		OhciAlign(((Addr_t)kmalloc(sizeof(OhciGTransferDescriptor_t) + OHCI_STRUCT_ALIGN)), 
			OHCI_STRUCT_ALIGN_BITS, OHCI_STRUCT_ALIGN);

	Controller->NullTd->BufferEnd = 0;
	Controller->NullTd->Cbp = 0;
	Controller->NullTd->NextTD = 0x0;
	Controller->NullTd->Flags = 0;

	/* Initialise ED Pool */
	for (i = 0; i < OHCI_POOL_NUM_ED; i++)
	{
		/* Allocate */
		Addr_t aSpace = (Addr_t)kmalloc(sizeof(OhciEndpointDescriptor_t) + OHCI_STRUCT_ALIGN);
		Controller->EDPool[i] = (OhciEndpointDescriptor_t*)
			OhciAlign(aSpace, OHCI_STRUCT_ALIGN_BITS, OHCI_STRUCT_ALIGN);

		/* Zero it out */
		memset((void*)Controller->EDPool[i], 0, sizeof(OhciEndpointDescriptor_t));

		/* Link to previous */
		Controller->EDPool[i]->NextED = 0;

		/* Set to skip and valid null Td */
		Controller->EDPool[i]->HcdFlags = 0;
		Controller->EDPool[i]->Flags = OHCI_EP_SKIP;
		Controller->EDPool[i]->HeadPtr =
			(Controller->EDPool[i]->TailPtr = 
				AddressSpaceGetMap(AddressSpaceGetCurrent(), (Addr_t)Controller->NullTd)) | 0x1;
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
		Controller->IntrTable->Ms16[i].Flags = OHCI_EP_SKIP;
	}

	/* Second level (8 ms) */
	EdLevel += 8 * sizeof(OhciEndpointDescriptor_t);
	for (i = 0; i < 8; i++)
	{
		Controller->IntrTable->Ms8[i].NextED = EdLevel + ((i / 2) * sizeof(OhciEndpointDescriptor_t));
		Controller->IntrTable->Ms8[i].NextEDVirtual = EdLevel + ((i / 2) * sizeof(OhciEndpointDescriptor_t));
		Controller->IntrTable->Ms8[i].Flags = OHCI_EP_SKIP;
	}

	/* Third level (4 ms) */
	EdLevel += 4 * sizeof(OhciEndpointDescriptor_t);
	for (i = 0; i < 4; i++)
	{
		Controller->IntrTable->Ms4[i].NextED = EdLevel + ((i / 2) * sizeof(OhciEndpointDescriptor_t));
		Controller->IntrTable->Ms4[i].NextEDVirtual = EdLevel + ((i / 2) * sizeof(OhciEndpointDescriptor_t));
		Controller->IntrTable->Ms4[i].Flags = OHCI_EP_SKIP;
	}

	/* Fourth level (2 ms) */
	EdLevel += 2 * sizeof(OhciEndpointDescriptor_t);
	for (i = 0; i < 2; i++)
	{
		Controller->IntrTable->Ms2[i].NextED = EdLevel + sizeof(OhciEndpointDescriptor_t);
		Controller->IntrTable->Ms2[i].NextEDVirtual = EdLevel + sizeof(OhciEndpointDescriptor_t);
		Controller->IntrTable->Ms2[i].Flags = OHCI_EP_SKIP;
	}

	/* Last level (1 ms) */
	Controller->IntrTable->Ms1[0].NextED = 0;
	Controller->IntrTable->Ms1[0].NextEDVirtual = 0;
	Controller->IntrTable->Ms1[0].Flags = OHCI_EP_SKIP;

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
	if (Controller->Registers->HcControl & OHCI_CONTROL_IR)
	{
		/* Ok, SMM has control, now give me my hat back */
		Temp = Controller->Registers->HcCommandStatus;
		Temp |= OHCI_COMMAND_OWNERSHIP;
		Controller->Registers->HcCommandStatus = Temp;

		/* Wait for InterruptRouting to clear */
		WaitForConditionWithFault(i, 
			(Controller->Registers->HcControl & OHCI_CONTROL_IR) == 0, 250, 10);

		if (i != 0)
		{
			/* Did not work, reset bit, try that */
			Controller->Registers->HcControl &= ~OHCI_CONTROL_IR;
			WaitForConditionWithFault(i, (Controller->Registers->HcControl & OHCI_CONTROL_IR) == 0, 250, 10);

			if (i != 0)
			{
				LogDebug("OHCI", "USB_OHCI: failed to clear routing bit");
				LogDebug("OHCI", "USB_OHCI: SMM Won't give us the Controller, we're backing down >(");
				
				AddressSpaceUnmap(AddressSpaceGetCurrent(), Controller->HccaSpace, PAGE_SIZE);
				kfree(Controller);
				return -1;
			}
		}
	}

	/* Is BIOS the bitch?? */
	else if (Controller->Registers->HcControl & OHCI_CONTROL_FSTATE_BITS)
	{
		if ((Controller->Registers->HcControl & OHCI_CONTROL_FSTATE_BITS) != OHCI_CONTROL_ACTIVE)
		{
			/* Resume Usb Operations */
			OhciSetMode(Controller, OHCI_CONTROL_ACTIVE);

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

	/* Done! */
	return 0;
}

/* Reset Controller */
void OhciReset(OhciController_t *Controller)
{
	/* Vars needed for reset procedure */
	uint32_t TempValue, Temp, FmInt;
	int i;

	/* Step 4. Verify HcFmInterval and save it  */
	FmInt = Controller->Registers->HcFmInterval;

	/* Sanity - What the fuck OHCI */
	if (OHCI_FMINTERVAL_GETFSMP(FmInt) == 0)
		FmInt |= OHCI_FMINTERVAL_FSMP(FmInt) << 16;

	/* Sanity interval once more */
	if ((FmInt & OHCI_FMINTERVAL_FIMASK) == 0)
		FmInt |= OHCI_FMINTERVAL_FI;

	/* We should check here if HcControl has RemoteWakeup Connected 
	 * and then set device to remote wake capable */

	/* Disable All Interrupts */
	Controller->Registers->HcInterruptDisable = (uint32_t)OHCI_INTR_MASTER;

	/* Perform a reset of HcCtrl Controller */
	OhciSetMode(Controller, OHCI_CONTROL_SUSPEND);
	StallMs(10);

	/* Set bit 0 to Request reboot */
	Temp = Controller->Registers->HcCommandStatus;
	Temp |= OHCI_COMMAND_RESET;
	Controller->Registers->HcCommandStatus = Temp;

	/* Wait for reboot (takes maximum of 10 ms) */
	WaitForConditionWithFault(i, (Controller->Registers->HcCommandStatus & OHCI_COMMAND_RESET) == 0, 50, 1);

	/* Sanity */
	if (i != 0)
	{
		LogDebug("OHCI", "Controller %u failed to reboot", Controller->HcdId);
		LogDebug("OHCI", "Reset Timeout :(");
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
	Controller->Registers->HcInterruptDisable = 
		(OHCI_INTR_SOF | OHCI_INTR_ROOTHUB_EVENT | OHCI_INTR_OWNERSHIP_EVENT);

	/* Clear INTR Status */
	Controller->Registers->HcInterruptStatus = ~(uint32_t)0;

	/* Set which interrupts we do want */
	Controller->Registers->HcInterruptEnable = (OHCI_INTR_SCHEDULING_OVERRUN | OHCI_INTR_PROCESS_HEAD |
		OHCI_INTR_RESUMEDETECT | OHCI_INTR_FATAL_ERROR | OHCI_INTR_FRAME_OVERFLOW | OHCI_INTR_MASTER);

	/* Set HcPeriodicStart to a value that is 90% of FrameInterval in HcFmInterval */
	TempValue = (FmInt & OHCI_FMINTERVAL_FIMASK);
	Controller->Registers->HcPeriodicStart = (TempValue / 10) * 9;

	/* Clear Lists, Mode, Ratio and IR */
	Temp = (Temp & ~(0x0000003C | OHCI_CONTROL_SUSPEND | 0x3 | 0x100));

	/* Set Ratio (4:1) and Mode (Operational) */
	Temp |= (0x3 | OHCI_CONTROL_ACTIVE);
	Controller->Registers->HcControl = Temp | 
		OHCI_CONTROL_ENABLE_ALL | OHCI_CONTROL_REMOTEWAKE;
}

/* Resets the controllre to a working state from initial */
void OhciSetup(OhciController_t *Controller)
{
	/* Vars needed for setup */
	UsbHc_t *HcCtrl;
	uint32_t TempValue = 0;
	int i;

	/* Step 1. Verify the Revision */
	TempValue = (Controller->Registers->HcRevision & 0xFF);
	if (TempValue != OHCI_REVISION
		&& TempValue != OHCI_REVISION_11)
	{
		LogDebug("OHCI", "Revision is wrong (0x%x), exiting :(", TempValue);
		AddressSpaceUnmap(AddressSpaceGetCurrent(), Controller->HccaSpace, PAGE_SIZE);
		kfree(Controller);
		return;
	}

	/* Step 2. Init Virtual Queues */
	OhciInitQueues(Controller);

	/* Step 3. Gain control of Controller */
	if (OhciTakeControl(Controller) == -1)
		return;

	/* Step 4. Reset Controller */
	OhciReset(Controller);

	/* Controller is now running! */
#ifdef _OHCI_DIAGNOSTICS_
	DebugPrint("OHCI: Controller %u Started, Control 0x%x, Ints 0x%x, FmInterval 0x%x\n",
		Controller->Id, Controller->Registers->HcControl, Controller->Registers->HcInterruptEnable, Controller->Registers->HcFmInterval);
#endif

	/* Check Power Mode */
	if (Controller->Registers->HcRhDescriptorA & (1 << 9))
	{
		Controller->PowerMode = OHCI_PWN_ALWAYS_ON;
		Controller->Registers->HcRhStatus = OHCI_STATUS_POWER_ENABLED;
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
			Controller->PowerMode = OHCI_PWM_PORT_CONTROLLED;
			Controller->Registers->HcRhDescriptorB = 0xFFFF0000;
		}
		else
		{
			/* Global Power Switch */
			Controller->Registers->HcRhDescriptorB = 0;
			Controller->Registers->HcRhStatus = OHCI_STATUS_POWER_ENABLED;
			Controller->PowerMode = OHCI_PWN_GLOBAL;
		}
	}

	/* Get Port count from (DescriptorA & 0x7F) */
	Controller->Ports = Controller->Registers->HcRhDescriptorA & 0x7F;

	/* Sanity */
	if (Controller->Ports > 15)
		Controller->Ports = 15;

	/* Set RhA */
	Controller->Registers->HcRhDescriptorA &= ~(0x00000000 | OHCI_DESCRIPTORA_DEVICETYPE);

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

#ifdef _OHCI_DIAGNOSTICS_
	DebugPrint("OHCI: Ports %u (power Mode %u, power delay %u)\n",
		Controller->Ports, Controller->PowerMode, TempValue);
#endif

	/* Setup HCD */
	HcCtrl = UsbInitController((void*)Controller, OhciController, Controller->Ports);

	/* Port Functions */
	HcCtrl->PortSetup = OhciPortStatus;

	/* Callback Functions */
	HcCtrl->RootHubCheck = OhciPortsCheck;
	HcCtrl->Reset = OhciReset;

	/* Endpoint Functions */
	HcCtrl->EndpointSetup = OhciEndpointSetup;
	HcCtrl->EndpointDestroy = OhciEndpointDestroy;

	/* Transaction Functions */
	HcCtrl->TransactionInit = OhciTransactionInit;
	HcCtrl->TransactionSetup = OhciTransactionSetup;
	HcCtrl->TransactionIn = OhciTransactionIn;
	HcCtrl->TransactionOut = OhciTransactionOut;
	HcCtrl->TransactionSend = OhciTransactionSend;
	HcCtrl->TransactionDestroy = OhciTransactionDestroy;

	Controller->HcdId = UsbRegisterController(HcCtrl);

	/* Setup Ports */
	for (i = 0; i < (int)Controller->Ports; i++)
	{
		int p = i;

		/* Make sure power is on */
		if (!(Controller->Registers->HcRhPortStatus[i] & OHCI_PORT_POWER))
			Controller->Registers->HcRhPortStatus[i] = OHCI_PORT_POWER;

		/* Check if Port is connected */
		if (Controller->Registers->HcRhPortStatus[i] & OHCI_PORT_CONNECTED)
			UsbEventCreate(UsbGetHcd(Controller->HcdId), p, HcdConnectedEvent);
	}

	/* Now we can enable hub events (and clear interrupts) */
	Controller->Registers->HcInterruptStatus &= ~(uint32_t)0;
	Controller->Registers->HcInterruptEnable = OHCI_INTR_ROOTHUB_EVENT;
}

/* Try to visualize the IntrTable */
void OhciVisualizeQueue(OhciController_t *Controller)
{
	/* For each of the 32 base's */
	int i;

	for (i = 0; i < 32; i++)
	{
		/* Get a base */
		OhciEndpointDescriptor_t *EpPtr = Controller->ED32[i];

		/* Keep going */
		while (EpPtr)
		{
			/* Print */
			LogRaw("0x%x -> ", (EpPtr->Flags & OHCI_EP_SKIP));

			/* Get next */
			EpPtr = (OhciEndpointDescriptor_t*)EpPtr->NextEDVirtual;
		}

		/* Newline */
		LogRaw("\n");
	}
}

/* ED Functions */
OhciEndpointDescriptor_t *OhciAllocateEp(OhciController_t *Controller, UsbTransferType_t Type)
{
	/* Vars */
	OhciEndpointDescriptor_t *Ed = NULL;
	int i;

	/* Pick a QH */
	SpinlockAcquire(&Controller->Lock);

	/* Grap it, locked operation */
	if (Type == ControlTransfer
		|| Type == BulkTransfer)
	{
		/* Grap Index */
		for (i = 0; i < OHCI_POOL_NUM_ED; i++)
		{
			/* Sanity */
			if (Controller->EDPool[i]->HcdFlags & OHCI_ED_ALLOCATED)
				continue;

			/* Yay!! */
			Controller->EDPool[i]->HcdFlags = OHCI_ED_ALLOCATED;
			Ed = Controller->EDPool[i];
			break;
		}

		/* Sanity */
		if (i == 50)
			kernel_panic("USB_OHCI::WTF RAN OUT OF EDS\n");
	}
	else if (Type == InterruptTransfer
		|| Type == IsochronousTransfer)
	{
		/* Allocate */
		Addr_t aSpace = (Addr_t)kmalloc(sizeof(OhciEndpointDescriptor_t) + OHCI_STRUCT_ALIGN);
		Ed = (OhciEndpointDescriptor_t*)OhciAlign(aSpace, OHCI_STRUCT_ALIGN_BITS, OHCI_STRUCT_ALIGN);

		/* Zero it out */
		memset(Ed, 0, sizeof(OhciEndpointDescriptor_t));
	}

	/* Release Lock */
	SpinlockRelease(&Controller->Lock);

	/* Done! */
	return Ed;
}

/* Initialises an EP descriptor */
void OhciEpInit(OhciEndpointDescriptor_t *Ed, UsbHcTransaction_t *FirstTd, UsbTransferType_t Type,
	size_t Address, size_t Endpoint, size_t PacketSize, UsbSpeed_t Speed)
{
	/* Set Head & Tail Td */
	if ((Addr_t)FirstTd->TransferDescriptor == OHCI_LINK_END)
	{
		Ed->HeadPtr = OHCI_LINK_END;
		Ed->TailPtr = 0;
	}
	else
	{
		/* Vars */
		Addr_t FirstTdAddr = (Addr_t)FirstTd->TransferDescriptor;
		Addr_t LastTd = 0;

		/* Get tail */
		UsbHcTransaction_t *FirstLink = FirstTd;
		while (FirstLink->Link)
			FirstLink = FirstLink->Link;
		LastTd = (Addr_t)FirstLink->TransferDescriptor;
		
		/* Get physical addresses */
		Ed->TailPtr = AddressSpaceGetMap(AddressSpaceGetCurrent(), (Addr_t)LastTd);
		Ed->HeadPtr = AddressSpaceGetMap(AddressSpaceGetCurrent(), (Addr_t)FirstTdAddr) | OHCI_LINK_END;
	}

	/* Shared flags */
	Ed->Flags = OHCI_EP_SKIP;
	Ed->Flags |= (Address & OHCI_EP_ADDRESS_MASK);
	Ed->Flags |= OHCI_EP_ENDPOINT(Endpoint);
	Ed->Flags |= OHCI_EP_INOUT_TD; /* Get PID from Td */
	Ed->Flags |= OHCP_EP_LOWSPEED((Speed == LowSpeed) ? 1 : 0);
	Ed->Flags |= OHCI_EP_MAXLEN(PacketSize);
	Ed->Flags |= OHCI_EP_TYPE(Type);

	/* Add for IsoChro */
	if (Type == IsochronousTransfer)
		Ed->Flags |= OHCI_EP_ISOCHRONOUS;
}

/* Td Functions */
Addr_t OhciAllocateTd(OhciEndpoint_t *Ep, UsbTransferType_t Type)
{
	/* Vars */
	OhciGTransferDescriptor_t *Td;
	Addr_t cIndex = 0;
	size_t i;

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
			if (Ep->TDPool[i]->Flags & OHCI_TD_ALLOCATED)
				continue;

			/* Yay!! */
			Ep->TDPool[i]->Flags |= OHCI_TD_ALLOCATED;
			cIndex = (Addr_t)i;
			break;
		}

		/* Sanity */
		if (i == Ep->TdsAllocated)
			kernel_panic("USB_OHCI::WTF ran out of TD's!!!!\n");
	}
	else if (Type == InterruptTransfer)
	{
		/* Allocate a new */
		Td = (OhciGTransferDescriptor_t*)OhciAlign(((Addr_t)kmalloc(sizeof(OhciGTransferDescriptor_t) + OHCI_STRUCT_ALIGN)), OHCI_STRUCT_ALIGN_BITS, OHCI_STRUCT_ALIGN);

		/* Null it */
		memset((void*)Td, 0, sizeof(OhciGTransferDescriptor_t));

		/* Set as index */
		cIndex = (Addr_t)Td;
	}
	else
	{
		/* Allocate iDescriptor */
		OhciITransferDescriptor_t *iTd = (OhciITransferDescriptor_t*)OhciAlign(((Addr_t)kmalloc(sizeof(OhciITransferDescriptor_t) + OHCI_STRUCT_ALIGN)), OHCI_STRUCT_ALIGN_BITS, OHCI_STRUCT_ALIGN);

		/* Null it */
		memset((void*)iTd, 0, sizeof(OhciITransferDescriptor_t));

		/* Set as index */
		cIndex = (Addr_t)iTd;
	}

	/* Release Lock */
	SpinlockRelease(&Ep->Lock);

	return cIndex;
}

/* Setup TD */
OhciGTransferDescriptor_t *OhciTdSetup(OhciEndpoint_t *Ep, UsbTransferType_t Type, 
	UsbPacket_t *pPacket, void **TDBuffer)
{
	/* Vars */
	OhciGTransferDescriptor_t *Td;
	Addr_t TDIndex;
	void *Buffer;

	/* Allocate a Td */
	TDIndex = OhciAllocateTd(Ep, Type);

	/* Grab a Td and a Buffer */
	Td = Ep->TDPool[TDIndex];
	Buffer = Ep->TDPoolBuffers[TDIndex];

	/* Set no link */
	Td->NextTD = OHCI_LINK_END;

	/* Setup the Td for a SETUP Td */
	Td->Flags = OHCI_TD_ALLOCATED;
	Td->Flags |= OHCI_TD_PID_SETUP;
	Td->Flags |= OHCI_TD_NO_IOC;
	Td->Flags |= OHCI_TD_TOGGLE_LOCAL;
	Td->Flags |= OHCI_TD_ACTIVE;

	/* Setup the SETUP Request */
	*TDBuffer = Buffer;
	memcpy(Buffer, (void*)pPacket, sizeof(UsbPacket_t));

	/* Set Td Buffer */
	Td->Cbp = AddressSpaceGetMap(AddressSpaceGetCurrent(), (VirtAddr_t)Buffer);
	Td->BufferEnd = Td->Cbp + sizeof(UsbPacket_t) - 1;

	return Td;
}

OhciGTransferDescriptor_t *OhciTdIo(OhciEndpoint_t *OhciEp, UsbTransferType_t Type,
	UsbHcEndpoint_t *Endpoint, uint32_t PId, size_t Length, void **TDBuffer)
{
	/* Vars */
	OhciGTransferDescriptor_t *Td = NULL;
	OhciITransferDescriptor_t *iTd = NULL;
	Addr_t TDIndex;
	void *Buffer;

	/* Allocate a Td */
	TDIndex = OhciAllocateTd(OhciEp, Type);

	/* Sanity */
	if (Type == ControlTransfer || Type == BulkTransfer){
		Td = OhciEp->TDPool[TDIndex];
		Buffer = OhciEp->TDPoolBuffers[TDIndex];
	}
	else if (Type == InterruptTransfer) {
		Td = (OhciGTransferDescriptor_t*)TDIndex;
		Buffer = (void*)kmalloc_a(PAGE_SIZE);
	}
	else
	{
		/* Calculate frame count */
		uint32_t FrameCount = DIVUP(Length, Endpoint->MaxPacketSize);
		uint32_t BufItr = 0;
		uint32_t FrameItr = 0;
		uint32_t Crossed = 0;

		/* Cast */
		iTd = (OhciITransferDescriptor_t*)TDIndex;

		/* Allocate a buffer */
		Buffer = (void*)kmalloc_a(Length);

		/* IF framecount is > 8, nono */
		if (FrameCount > 8)
			FrameCount = 8;

		/* Setup */
		iTd->Flags = 0;
		iTd->Flags |= OHCI_TD_FRAMECOUNT(FrameCount - 1);
		iTd->Flags |= OHCI_TD_NO_IOC;

		/* Buffer */
		iTd->Bp0 = AddressSpaceGetMap(AddressSpaceGetCurrent(), (VirtAddr_t)Buffer);
		iTd->BufferEnd = iTd->Bp0 + Length - 1;

		/* Setup offsets */
		while (FrameCount)
		{
			/* Set offset 0 */
			iTd->Offsets[FrameItr] = (BufItr & 0xFFF);
			iTd->Offsets[FrameItr] = ((Crossed & 0x1) << 12);

			/* Increase buffer */
			BufItr += Endpoint->MaxPacketSize;

			/* Sanity */
			if (BufItr >= PAGE_SIZE)
			{
				/* Reduce, set crossed */
				BufItr -= PAGE_SIZE;
				Crossed = 1;
			}

			/* Set iterators */
			FrameItr++;
			FrameCount--;
		}

		/* EOL */
		iTd->NextTD = OHCI_LINK_END;

		/* Done */
		return (OhciGTransferDescriptor_t*)iTd;
	}

	/* EOL */
	iTd->NextTD = OHCI_LINK_END;

	/* Setup the Td for a IO Td */
	Td->Flags = OHCI_TD_ALLOCATED;
	Td->Flags |= OHCI_TD_SHORTPACKET;
	Td->Flags |= PId;
	Td->Flags |= OHCI_TD_NO_IOC;
	Td->Flags |= OHCI_TD_TOGGLE_LOCAL;
	Td->Flags |= OHCI_TD_ACTIVE;
	Td->Flags |= (Endpoint->Toggle << 24);

	/* Store buffer */
	*TDBuffer = Buffer;

	/* Bytes to transfer?? */
	if (Length > 0) {
		Td->Cbp = AddressSpaceGetMap(AddressSpaceGetCurrent(), (VirtAddr_t)Buffer);
		Td->BufferEnd = Td->Cbp + Length - 1;
	}
	else {
		Td->Cbp = 0;
		Td->BufferEnd = 0;
	}

	/* Done! */
	return Td;
}

/* Endpoint Functions */
void OhciEndpointSetup(void *Controller, UsbHcEndpoint_t *Endpoint)
{
	/* Cast */
	OhciController_t *oCtrl = (OhciController_t*)Controller;
	Addr_t BufAddr = 0, BufAddrMax = 0;
	Addr_t Pool, PoolPhys;
	size_t i;

	/* Allocate a structure */
	OhciEndpoint_t *oEp = (OhciEndpoint_t*)kmalloc(sizeof(OhciEndpoint_t));

	/* Construct the lock */
	SpinlockReset(&oEp->Lock);

	/* Woah */
	_CRT_UNUSED(oCtrl);

	/* Now, we want to allocate some TD's 
	 * but it largely depends on what kind of endpoint this is */
	if (Endpoint->Type == EndpointControl)
		oEp->TdsAllocated = OHCI_ENDPOINT_MIN_ALLOCATED;
	else if (Endpoint->Type == EndpointBulk)
	{
		/* Depends on the maximum transfer */
		oEp->TdsAllocated = DEVICEMANAGER_MAX_IO_SIZE / Endpoint->MaxPacketSize;
		
		/* Take in account control packets and other stuff */
		oEp->TdsAllocated += OHCI_ENDPOINT_MIN_ALLOCATED;
	}
	else
	{
		/* We handle interrupt & iso dynamically 
		 * we don't predetermine their sizes */
		oEp->TdsAllocated = 0;
		Endpoint->AttachedData = oEp;
		return;
	}

	/* Now, we do the actual allocation */
	oEp->TDPool = (OhciGTransferDescriptor_t**)kmalloc(sizeof(OhciGTransferDescriptor_t*) * oEp->TdsAllocated);
	oEp->TDPoolBuffers = (Addr_t**)kmalloc(sizeof(Addr_t*) * oEp->TdsAllocated);
	oEp->TDPoolPhysical = (Addr_t*)kmalloc(sizeof(Addr_t) * oEp->TdsAllocated);

	/* Allocate a TD block */
	Pool = (Addr_t)kmalloc((sizeof(OhciGTransferDescriptor_t) * oEp->TdsAllocated) + OHCI_STRUCT_ALIGN);
	Pool = OhciAlign(Pool, OHCI_STRUCT_ALIGN_BITS, OHCI_STRUCT_ALIGN);
	PoolPhys = AddressSpaceGetMap(AddressSpaceGetCurrent(), Pool);

	/* Allocate buffers */
	BufAddr = (Addr_t)kmalloc_a(PAGE_SIZE);
	BufAddrMax = BufAddr + PAGE_SIZE - 1;

	/* Memset it */
	memset((void*)Pool, 0, sizeof(OhciGTransferDescriptor_t) * oEp->TdsAllocated);

	/* Iterate it */
	for (i = 0; i < oEp->TdsAllocated; i++)
	{
		/* Set */
		oEp->TDPool[i] = (OhciGTransferDescriptor_t*)Pool;
		oEp->TDPoolPhysical[i] = PoolPhys;

		/* Allocate another page? */
		if (BufAddr > BufAddrMax)
		{
			BufAddr = (Addr_t)kmalloc_a(PAGE_SIZE);
			BufAddrMax = BufAddr + PAGE_SIZE - 1;
		}

		/* Setup Buffer */
		oEp->TDPoolBuffers[i] = (Addr_t*)BufAddr;
		oEp->TDPool[i]->Cbp = AddressSpaceGetMap(AddressSpaceGetCurrent(), BufAddr);
		oEp->TDPool[i]->NextTD = 0x1;

		/* Increase */
		Pool += sizeof(OhciGTransferDescriptor_t);
		PoolPhys += sizeof(OhciGTransferDescriptor_t);
		BufAddr += Endpoint->MaxPacketSize;
	}

	/* Done! Save */
	Endpoint->AttachedData = oEp;
}

void OhciEndpointDestroy(void *Controller, UsbHcEndpoint_t *Endpoint)
{
	/* Cast */
	OhciController_t *oCtrl = (OhciController_t*)Controller;
	OhciEndpoint_t *oEp = (OhciEndpoint_t*)Endpoint->AttachedData;
	
	/* Sanity */
	if (oEp == NULL)
		return;

	/* Woah */
	_CRT_UNUSED(oCtrl);

	/* Sanity */
	if (oEp->TdsAllocated != 0)
	{
		/* Vars */
		OhciGTransferDescriptor_t *oTd = oEp->TDPool[0];
		size_t i;

		/* Let's free all those resources */
		for (i = 0; i < oEp->TdsAllocated; i++)
		{
			/* free buffer */
			kfree(oEp->TDPoolBuffers[i]);
		}

		/* Free blocks */
		kfree(oTd);
		kfree(oEp->TDPoolBuffers);
		kfree(oEp->TDPoolPhysical);
		kfree(oEp->TDPool);
	}

	/* Free the descriptor */
	kfree(oEp);
}

/* Bandwidth Functions */
int OhciCalculateQueue(OhciController_t *Controller, size_t Interval, uint32_t Bandwidth)
{
	/* Vars */
	int	Queue = -1;
	size_t i;

	/* iso periods can be huge; iso tds specify frame numbers */
	if (Interval > OHCI_BANDWIDTH_PHASES)
		Interval = OHCI_BANDWIDTH_PHASES;

	/* Find the least loaded queue */
	for (i = 0; i < Interval; i++) {
		if (Queue < 0 || Controller->Bandwidth[Queue] > Controller->Bandwidth[i]) {
			int	j;

			/* Usb 1.1 says 90% of one frame must be ISOC / INTR */
			for (j = i; j < OHCI_BANDWIDTH_PHASES; j += Interval) {
				if ((Controller->Bandwidth[j] + Bandwidth) > 900)
					break;
			}

			/* Sanity */
			if (j < OHCI_BANDWIDTH_PHASES)
				continue;

			/* Store */
			Queue = i;
		}
	}

	/* Done! */
	return Queue;
}

/* Transaction Functions */

/* This one prepaires an ED */
void OhciTransactionInit(void *Controller, UsbHcRequest_t *Request)
{
	/* Vars */
	OhciController_t *Ctrl = (OhciController_t*)Controller;
	OhciEndpointDescriptor_t *Ed = NULL;

	/* Allocate an EP */
	Ed = OhciAllocateEp(Ctrl, Request->Type);

	/* Mark it inactive for now */
	Ed->Flags |= OHCI_EP_SKIP;

	/* Calculate bus time */
	Ed->Bandwidth =
		(UsbCalculateBandwidth(Request->Speed, 
		Request->Endpoint->Direction, Request->Type, Request->Endpoint->MaxPacketSize) / 1000);

	/* Store */
	Request->Data = Ed;

	/* Set as not Completed for start */
	Request->Status = TransferNotProcessed;
}

/* This one prepaires an setup Td */
UsbHcTransaction_t *OhciTransactionSetup(void *Controller, UsbHcRequest_t *Request)
{
	/* Vars */
	OhciController_t *Ctrl = (OhciController_t*)Controller;
	UsbHcTransaction_t *Transaction;

	/* Unused */
	_CRT_UNUSED(Ctrl);

	/* Allocate Transaction */
	Transaction = (UsbHcTransaction_t*)kmalloc(sizeof(UsbHcTransaction_t));
	Transaction->IoBuffer = 0;
	Transaction->IoLength = 0;
	Transaction->Link = NULL;

	/* Create the Td */
	Transaction->TransferDescriptor = (void*)OhciTdSetup(Request->Endpoint->AttachedData, 
		Request->Type, &Request->Packet, &Transaction->TransferBuffer);

	/* Done */
	return Transaction;
}

/* This one prepaires an in Td */
UsbHcTransaction_t *OhciTransactionIn(void *Controller, UsbHcRequest_t *Request)
{
	/* Vars */
	OhciController_t *Ctrl = (OhciController_t*)Controller;
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
	Transaction->TransferDescriptor = (void*)OhciTdIo(Request->Endpoint->AttachedData, 
		Request->Type, Request->Endpoint, OHCI_TD_PID_IN, Request->IoLength,
		&Transaction->TransferBuffer);

	/* If previous Transaction */
	if (Request->Transactions != NULL)
	{
		OhciGTransferDescriptor_t *PrevTd;
		UsbHcTransaction_t *cTrans = Request->Transactions;

		while (cTrans->Link)
			cTrans = cTrans->Link;

		PrevTd = (OhciGTransferDescriptor_t*)cTrans->TransferDescriptor;
		PrevTd->NextTD = AddressSpaceGetMap(AddressSpaceGetCurrent(), 
			(VirtAddr_t)Transaction->TransferDescriptor);
	}

	/* We might need a copy */
	if (Request->Type == InterruptTransfer
		|| Request->Type == IsochronousTransfer)
	{
		/* Allocate TD */
		Transaction->TransferDescriptorCopy = 
			(void*)OhciAllocateTd(Request->Endpoint->AttachedData, Request->Type);
	}

	/* Done */
	return Transaction;
}

/* This one prepaires an out Td */
UsbHcTransaction_t *OhciTransactionOut(void *Controller, UsbHcRequest_t *Request)
{
	/* Vars */
	OhciController_t *Ctrl = (OhciController_t*)Controller;
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
	Transaction->TransferDescriptor = (void*)OhciTdIo(Request->Endpoint->AttachedData, 
		Request->Type, Request->Endpoint, OHCI_TD_PID_OUT, Request->IoLength,
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
		PrevTd->NextTD = AddressSpaceGetMap(AddressSpaceGetCurrent(), (VirtAddr_t)Transaction->TransferDescriptor);
	}

	/* We might need a copy */
	if (Request->Type == InterruptTransfer
		|| Request->Type == IsochronousTransfer)
	{
		/* Allocate TD */
		Transaction->TransferDescriptorCopy =
			(void*)OhciAllocateTd(Request->Endpoint->AttachedData, Request->Type);
	}

	/* Done */
	return Transaction;
}

/* This one queues the Transaction up for processing */
void OhciTransactionSend(void *Controller, UsbHcRequest_t *Request)
{
	/* Wuhu */
	UsbHcTransaction_t *Transaction = Request->Transactions;
	OhciController_t *Ctrl = (OhciController_t*)Controller;
	UsbTransferStatus_t Completed = TransferFinished;
	OhciEndpointDescriptor_t *Ep = NULL;
	OhciGTransferDescriptor_t *Td = NULL;
	uint32_t CondCode;
	Addr_t EdAddress;

	/*************************
	****** SETUP PHASE ******
	*************************/

	/* Cast */
	Ep = (OhciEndpointDescriptor_t*)Request->Data;

	/* Get physical */
	EdAddress = AddressSpaceGetMap(AddressSpaceGetCurrent(), (VirtAddr_t)Ep);

	/* Set as not Completed for start */
	Request->Status = TransferNotProcessed;

	/* Add dummy Td to end
	 * But we have to keep the endpoint toggle */
	if (Request->Type != IsochronousTransfer) {
		CondCode = Request->Endpoint->Toggle;
		UsbTransactionOut(UsbGetHcd(Ctrl->HcdId), Request,
			(Request->Type == ControlTransfer) ? 1 : 0, NULL, 0);
		Request->Endpoint->Toggle = CondCode;
		CondCode = 0;
	}

	/* Iterate and set last to INT */
	Transaction = Request->Transactions;
	while (Transaction->Link
		&& Transaction->Link->Link)
	{
#ifdef _OHCI_DIAGNOSTICS_
		printf("Td (Addr 0x%x) Flags 0x%x, Cbp 0x%x, BufferEnd 0x%x, Next Td 0x%x\n", 
			AddressSpaceGetMap(AddressSpaceGetCurrent(), (VirtAddr_t)Td), Td->Flags, Td->Cbp, Td->BufferEnd, Td->NextTD);
#endif
		/* Next */
		Transaction = Transaction->Link;
	}

	/* Retrieve Td */
#ifndef _OHCI_DIAGNOSTICS_
	Td = (OhciGTransferDescriptor_t*)Transaction->TransferDescriptor;
	Td->Flags &= ~(OHCI_TD_NO_IOC);
#endif

	/* Initialize the allocated ED */
	OhciEpInit(Ep, Request->Transactions, Request->Type,
		Request->Device->Address, Request->Endpoint->Address,
		Request->Endpoint->MaxPacketSize, Request->Speed);

	/* Add this Transaction to list */
	list_append((list_t*)Ctrl->TransactionList, list_create_node(0, Request));

	/* Remove Skip */
	Ep->Flags &= ~(OHCI_EP_SKIP);
	Ep->HeadPtr &= ~(0x1);

#ifdef _OHCI_DIAGNOSTICS_
	printf("Ed Address 0x%x, Flags 0x%x, Ed Tail 0x%x, Ed Head 0x%x, Next 0x%x\n", EdAddress, ((OhciEndpointDescriptor_t*)Request->Data)->Flags,
		((OhciEndpointDescriptor_t*)Request->Data)->TailPtr, ((OhciEndpointDescriptor_t*)Request->Data)->HeadPtr, ((OhciEndpointDescriptor_t*)Request->Data)->NextED);
#endif

	/*************************
	**** LINKING PHASE ******
	*************************/
	
	/* Now lets try the Transaction */
	SpinlockAcquire(&Ctrl->Lock);

	/* Add them to list */
	if (Request->Type == ControlTransfer)
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
		}
		else
		{
			/* Add it HcControl/BulkCurrentED */
			Ctrl->Registers->HcControlHeadED =
				Ctrl->Registers->HcControlCurrentED = EdAddress;

			/* Increase */
			Ctrl->TransactionsWaitingControl++;

			/* Set Lists Filled (Enable Them) */
			Ctrl->Registers->HcCommandStatus |= OHCI_COMMAND_CONTROL_ACTIVE;
		}
	}
	else if (Request->Type == BulkTransfer)
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
		}
		else
		{
			/* Add it HcControl/BulkCurrentED */
			Ctrl->Registers->HcBulkHeadED =
				Ctrl->Registers->HcBulkCurrentED = EdAddress;

			/* Increase */
			Ctrl->TransactionsWaitingBulk++;

			/* Set Lists Filled (Enable Them) */
			Ctrl->Registers->HcCommandStatus |= OHCI_COMMAND_BULK_ACTIVE;
		}
	}
	else
	{
		/* Interrupt & Isochronous */

		/* Vars */
		OhciEndpointDescriptor_t *IEd = NULL;
		uint32_t Period = 32;
		int Queue = 0, Index = 0;

		/* Update saved copies, now all is prepaired */
		Transaction = Request->Transactions;
		while (Transaction)
		{
			/* Do an exact copy */
			memcpy(Transaction->TransferDescriptorCopy, Transaction->TransferDescriptor,
				(Request->Type == InterruptTransfer) ? sizeof(OhciGTransferDescriptor_t) :
				sizeof(OhciITransferDescriptor_t));

			/* Next */
			Transaction = Transaction->Link;
		}

		/* Find queue for this ED */
		Queue = OhciCalculateQueue(Ctrl, Request->Endpoint->Interval, Ep->Bandwidth);

		/* Sanity */
		assert(Queue >= 0);

		/* DISABLE SCHEDULLER!!! */
		Ctrl->Registers->HcControl &= ~(OHCI_CONTROL_PERIODIC_ACTIVE | OHCI_CONTROL_ISOC_ACTIVE);

		/* Calculate period */
		POW2(Request->Endpoint->Interval, Period);

		/* Get correct period list */
		if (Period == 1)
			IEd = &Ctrl->IntrTable->Ms1[0];
		else if (Period == 2) {
			IEd = &Ctrl->IntrTable->Ms2[Ctrl->I2];
			Index = Ctrl->I2;
			INCLIMIT(Ctrl->I2, 2);
		}
		else if (Period == 4) {
			IEd = &Ctrl->IntrTable->Ms4[Ctrl->I4];
			Index = Ctrl->I4;
			INCLIMIT(Ctrl->I4, 4);
		}
		else if (Period == 8) {
			IEd = &Ctrl->IntrTable->Ms8[Ctrl->I8];
			Index = Ctrl->I8;
			INCLIMIT(Ctrl->I8, 8);
		}
		else if (Period == 16) {
			IEd = &Ctrl->IntrTable->Ms16[Ctrl->I16];
			Index = Ctrl->I16;
			INCLIMIT(Ctrl->I16, 16);
		}
		else {
			/* 32 */
			IEd = Ctrl->ED32[Ctrl->I32];
			Index = Ctrl->I32;

			/* Insert it */
			Ep->NextEDVirtual = (Addr_t)IEd;
			Ep->NextED = AddressSpaceGetMap(AddressSpaceGetCurrent(), (VirtAddr_t)IEd);

			/* Make int-table point to this */
			Ctrl->HCCA->InterruptTable[Ctrl->I32] = EdAddress;
			Ctrl->ED32[Ctrl->I32] = Ep;

			/* Inc & Done */
			INCLIMIT(Ctrl->I32, 32);
		}

		/* Sanity */
		if (Period != 32)
		{
			/* Insert it */
			Ep->NextEDVirtual = IEd->NextEDVirtual;
			Ep->NextED = IEd->NextED;
			
			IEd->NextED = EdAddress;
			IEd->NextEDVirtual = (Addr_t)Ep;
		}

		/* Store ed info */
		Ep->HcdFlags |= OHCI_ED_SET_QUEUE(Queue);
		Ep->HcdFlags |= OHCI_ED_SET_PERIOD(Period);
		Ep->HcdFlags |= OHCI_ED_SET_INDEX(Index);

		/* ENABLE SCHEDULEER */
		Ctrl->Registers->HcControl |= OHCI_CONTROL_PERIODIC_ACTIVE | OHCI_CONTROL_ISOC_ACTIVE;
	}

	/* Release lock */
	SpinlockRelease(&Ctrl->Lock);

	/* Sanity */
	if (Request->Type == InterruptTransfer
		|| Request->Type == IsochronousTransfer)
		return;

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
	/* Wait for interrupt */
	SchedulerSleepThread((Addr_t*)Request->Data);

	/* Yield */
	_ThreadYield();
#endif

	/*************************
	*** VALIDATION PHASE ****
	*************************/

	/* Check Conditions (WithOUT dummy) */
#ifdef _OHCI_DIAGNOSTICS_
	printf("Ed Flags 0x%x, Ed Tail 0x%x, Ed Head 0x%x\n", ((OhciEndpointDescriptor_t*)Request->Data)->Flags,
		((OhciEndpointDescriptor_t*)Request->Data)->TailPtr, ((OhciEndpointDescriptor_t*)Request->Data)->HeadPtr);
#endif
	Transaction = Request->Transactions;
	while (Transaction->Link)
	{
		/* Cast and get the transfer code */
		Td = (OhciGTransferDescriptor_t*)Transaction->TransferDescriptor;
		CondCode = OHCI_TD_GET_CC(Td->Flags);
#ifdef _OHCI_DIAGNOSTICS_
		printf("Td Flags 0x%x, Cbp 0x%x, BufferEnd 0x%x, Td Condition Code %u (%s)\n", Td->Flags, Td->Cbp, Td->BufferEnd, CondCode, OhciErrorMessages[CondCode]);
#endif

		if (CondCode == 0 && Completed == TransferFinished)
			Completed = TransferFinished;
		else
		{
			if (CondCode == 4)
				Completed = TransferStalled;
			else if (CondCode == 3)
				Completed = TransferInvalidToggles;
			else if (CondCode == 2 || CondCode == 1)
				Completed = TransferBabble;
			else if (CondCode == 5)
				Completed = TransferNotResponding;
			else {
				LogDebug("OHCI", "Error: 0x%x (%s)", CondCode, OhciErrorMessages[CondCode]);
				Completed = TransferInvalidData;
			}
			break;
		}

		Transaction = Transaction->Link;
	}

#ifdef _OHCI_DIAGNOSTICS_
	printf("HcDoneHead: 0x%x\n", Ctrl->HCCA->HeadDone);
	printf("Current Frame 0x%x (HCCA), HcFrameNumber 0x%x, HcFrameInterval 0x%x, HcFrameRemaining 0x%x\n", Ctrl->HCCA->CurrentFrame,
		Ctrl->Registers->HcFmNumber, Ctrl->Registers->HcFmInterval, Ctrl->Registers->HcFmRemaining);
#endif

	/* Lets see... */
	if (Completed == TransferFinished)
	{
		/* Build Buffer */
		Transaction = Request->Transactions;

		while (Transaction->Link)
		{
			/* Copy Data? */
			if (Transaction->IoBuffer != NULL && Transaction->IoLength != 0)
				memcpy(Transaction->IoBuffer, Transaction->TransferBuffer, Transaction->IoLength);

			/* Next Link */
			Transaction = Transaction->Link;
		}
	}

	/* Update Status */
	Request->Status = Completed;

#ifdef _OHCI_DIAGNOSTICS_
	for (;;);
#endif
}

/* This one cleans up */
void OhciTransactionDestroy(void *Controller, UsbHcRequest_t *Request)
{
	/* Vars */
	UsbHcTransaction_t *Transaction = Request->Transactions;
	OhciController_t *Ctrl = (OhciController_t*)Controller;
	list_node_t *Node = NULL;

	/* Unallocate Ed */
	if (Request->Type == ControlTransfer
		|| Request->Type == BulkTransfer)
	{
		/* Iterate and reset */
		while (Transaction)
		{
			/* Cast */
			OhciGTransferDescriptor_t *Td =
				(OhciGTransferDescriptor_t*)Transaction->TransferDescriptor;

			/* Memset */
			memset((void*)Td, 0, sizeof(OhciGTransferDescriptor_t));

			/* Next */
			Transaction = Transaction->Link;
		}

		/* Reset the ED */
		memset(Request->Data, 0, sizeof(OhciEndpointDescriptor_t));
	}
	else
	{
		/* Unhook ED from the list it's in */
		SpinlockAcquire(&Ctrl->Lock);

		/* DISABLE SCHEDULLER!!! */
		Ctrl->Registers->HcControl &= ~(OHCI_CONTROL_PERIODIC_ACTIVE | OHCI_CONTROL_ISOC_ACTIVE);

		/* Iso / Interrupt */
		OhciEndpointDescriptor_t *Ed = (OhciEndpointDescriptor_t*)Request->Data;
		OhciEndpointDescriptor_t *IEd, *PrevIEd;
		uint32_t Period = OHCI_ED_GET_PERIOD(Ed->HcdFlags);
		int Index = OHCI_ED_GET_INDEX(Ed->HcdFlags);

		/* Get correct queue */
		if (Period == 1)
			IEd = &Ctrl->IntrTable->Ms1[Index];
		else if (Period == 2)
			IEd = &Ctrl->IntrTable->Ms2[Index];
		else if (Period == 4)
			IEd = &Ctrl->IntrTable->Ms4[Index];
		else if (Period == 8)
			IEd = &Ctrl->IntrTable->Ms8[Index];
		else if (Period == 16)
			IEd = &Ctrl->IntrTable->Ms16[Index];
		else
			IEd = Ctrl->ED32[Index];

		/* Iterate */
		PrevIEd = NULL;
		while (IEd != Ed)
		{
			/* Sanity */
			if (IEd->NextED == 0
				|| IEd->NextED == 0x1)
			{
				IEd = NULL;
				break;
			}

			/* Save */
			PrevIEd = IEd;

			/* Go to next */
			IEd = (OhciEndpointDescriptor_t*)IEd->NextEDVirtual;
		}

		/* Sanity */
		if (IEd != NULL)
		{
			/* Either we are first, or we are not */
			if (PrevIEd == NULL
				&& Period == 32)
			{
				/* Only special case for 32 period */
				Ctrl->ED32[Index] = (OhciEndpointDescriptor_t*)Ed->NextEDVirtual;
				Ctrl->HCCA->InterruptTable[Index] = Ed->NextED;
			}
			else if (PrevIEd != NULL)
			{
				/* Make it skip over */
				PrevIEd->NextED = Ed->NextED;
				PrevIEd->NextEDVirtual = Ed->NextEDVirtual;
			}
		}

		/* ENABLE SCHEDULEER */
		Ctrl->Registers->HcControl |= OHCI_CONTROL_PERIODIC_ACTIVE | OHCI_CONTROL_ISOC_ACTIVE;

		/* Done */
		SpinlockRelease(&Ctrl->Lock);

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

		/* Remove from list */
		_foreach(Node, ((list_t*)Ctrl->TransactionList)) {
			if (Node->data == Request)
				break;
		}

		/* Sanity */
		if (Node != NULL) {
			list_remove_by_node((list_t*)Ctrl->TransactionList, Node);
			kfree(Node);
		}
	}
}

/* Reload Controller */
void OhciReloadControlBulk(OhciController_t *Controller, UsbTransferType_t TransferType)
{
	/* So now, before waking up a sleeper we see if Transactions are pending
	* if they are, we simply copy the queue over to the current */
	SpinlockAcquire(&Controller->Lock);

	/* Any Controls waiting? */
	if (TransferType == 0)
	{
		if (Controller->TransactionsWaitingControl > 0)
		{
			/* Get physical of Ed */
			Addr_t EdPhysical = AddressSpaceGetMap(AddressSpaceGetCurrent(), Controller->TransactionQueueControl);

			/* Set it */
			Controller->Registers->HcControlHeadED =
				Controller->Registers->HcControlCurrentED = EdPhysical;

			/* Start queue */
			Controller->Registers->HcCommandStatus |= OHCI_COMMAND_CONTROL_ACTIVE;
		}

		/* Reset control queue */
		Controller->TransactionQueueControl = 0;
		Controller->TransactionsWaitingControl = 0;
	}
	else if (TransferType == 1)
	{
		/* Bulk */
		if (Controller->TransactionsWaitingBulk > 0)
		{
			/* Get physical of Ed */
			Addr_t EdPhysical = AddressSpaceGetMap(AddressSpaceGetCurrent(), Controller->TransactionQueueBulk);

			/* Add it to queue */
			Controller->Registers->HcBulkHeadED =
				Controller->Registers->HcBulkCurrentED = EdPhysical;

			/* Start queue */
			Controller->Registers->HcCommandStatus |= OHCI_COMMAND_BULK_ACTIVE;
		}

		/* Reset control queue */
		Controller->TransactionQueueBulk = 0;
		Controller->TransactionsWaitingBulk = 0;
	}

	/* Done */
	SpinlockRelease(&Controller->Lock);
}

/* Process Done Queue */
void OhciProcessDoneQueue(OhciController_t *Controller, Addr_t DoneHeadAddr)
{
	/* Get transaction list */
	list_t *Transactions = (list_t*)Controller->TransactionList;

	/* Get Ed with the same td address as DoneHeadAddr */
	foreach(Node, Transactions)
	{
		/* Cast UsbRequest */
		UsbHcRequest_t *HcRequest = (UsbHcRequest_t*)Node->data;

		/* Get Ed */
		OhciEndpointDescriptor_t *Ed = (OhciEndpointDescriptor_t*)HcRequest->Data;
		UsbTransferType_t TransferType = (UsbTransferType_t)((Ed->Flags >> 27) & 0xF);

		/* Get transaction list */
		UsbHcTransaction_t *tList = (UsbHcTransaction_t*)HcRequest->Transactions;

		/* Is it this? */
		while (tList)
		{
			/* Get physical of TD */
			Addr_t TdPhysical = AddressSpaceGetMap(AddressSpaceGetCurrent(), 
				(VirtAddr_t)tList->TransferDescriptor);

			/* Is it this one? */
			if (DoneHeadAddr == TdPhysical)
			{
				/* Depending on type */
				if (TransferType == ControlTransfer
					|| TransferType == BulkTransfer)
				{
					/* Reload */
					OhciReloadControlBulk(Controller, TransferType);

					/* Wake a node */
					SchedulerWakeupOneThread((Addr_t*)Ed);

					/* Remove from list */
					list_remove_by_node(Transactions, Node);

					/* Cleanup node */
					kfree(Node);
				}
				else if (TransferType == InterruptTransfer
					|| TransferType == IsochronousTransfer)
				{
					/* Re-Iterate */
					UsbHcTransaction_t *lIterator = HcRequest->Transactions;
					int SwitchToggles = HcRequest->TransactionCount % 2;
					int ErrorTransfer = 0;

					/* Copy data if not dummy */
					while (lIterator)
					{
						/* Get Td */
						OhciGTransferDescriptor_t *Td =
							(OhciGTransferDescriptor_t*)lIterator->TransferDescriptor;
						
						/* Get condition-code */
						int ConditionCode = OHCI_TD_GET_CC(Td->Flags);

						/* Sanity */
						if (ConditionCode != 0
							|| ErrorTransfer)
							ErrorTransfer = 1;
						else
						{
							/* Let's see */
							if (lIterator->IoLength != 0)
								memcpy(lIterator->IoBuffer, lIterator->TransferBuffer, lIterator->IoLength);

							/* Switch toggle */
							if (TransferType == InterruptTransfer
								&& SwitchToggles)
							{
								OhciGTransferDescriptor_t *__Td =
									(OhciGTransferDescriptor_t*)lIterator->TransferDescriptorCopy;

								/* Clear Toggle */
								__Td->Flags &= ~OHCI_TD_TOGGLE;

								/* Set it? */
								if (HcRequest->Endpoint->Toggle)
									__Td->Flags |= OHCI_TD_TOGGLE;

								/* Switch toggle bit */
								HcRequest->Endpoint->Toggle =
									(HcRequest->Endpoint->Toggle == 1) ? 0 : 1;
							}

							/* Restart Td */
							memcpy(lIterator->TransferDescriptor,
								lIterator->TransferDescriptorCopy,
								TransferType == InterruptTransfer ?
								sizeof(OhciGTransferDescriptor_t) : sizeof(OhciITransferDescriptor_t));
						}

						/* Eh, next link */
						lIterator = lIterator->Link;
					}

					/* Callback */
					if (HcRequest->Callback != NULL)
						HcRequest->Callback->Callback(HcRequest->Callback->Args, 
							ErrorTransfer == 1 ? TransferStalled : TransferFinished);

					/* Restart Ed */
					if (!ErrorTransfer)
						Ed->HeadPtr = 
							AddressSpaceGetMap(AddressSpaceGetCurrent(), 
								(VirtAddr_t)HcRequest->Transactions->TransferDescriptor);
				}

				/* Done */
				return;
			}

			/* Go to next */
			tList = tList->Link;
		}
	}
}

/* Interrupt Handler */
int OhciInterruptHandler(void *Args)
{
	/* Vars */
	MCoreDevice_t *mDevice = (MCoreDevice_t*)Args;
	OhciController_t *Controller = (OhciController_t*)mDevice->Driver.Data;
	uint32_t IntrState = 0;

	/* Is this our interrupt ? */
	if (Controller->HCCA->HeadDone != 0)
	{
		/* Acknowledge */
		IntrState = OHCI_INTR_PROCESS_HEAD;

		if (Controller->HCCA->HeadDone & 0x1)
		{
			/* Get rest of interrupts, since head_done has halted */
			IntrState |= (Controller->Registers->HcInterruptStatus 
				& Controller->Registers->HcInterruptEnable);
		}
	}
	else
	{
		/* Was it this Controller that made the interrupt?
		* We only want the interrupts we have set as enabled */
		IntrState = (Controller->Registers->HcInterruptStatus 
			& Controller->Registers->HcInterruptEnable);

		/* Sanity */
		if (IntrState == 0)
			return X86_IRQ_NOT_HANDLED;
	}

	/* Disable Interrupts */
	Controller->Registers->HcInterruptDisable = (uint32_t)OHCI_INTR_MASTER;

	/* Fatal Error? */
	if (IntrState & OHCI_INTR_FATAL_ERROR)
	{
		/* Port does not matter here */
		UsbEventCreate(UsbGetHcd(Controller->HcdId), 0, HcdFatalEvent);

		/* Done */
		return X86_IRQ_HANDLED;
	}

	/* Flag for end of frame Type interrupts */
	if (IntrState & (OHCI_INTR_SCHEDULING_OVERRUN | OHCI_INTR_PROCESS_HEAD 
		| OHCI_INTR_SOF | OHCI_INTR_FRAME_OVERFLOW))
		IntrState |= OHCI_INTR_MASTER;

	/* Scheduling Overrun? */
	if (IntrState & OHCI_INTR_SCHEDULING_OVERRUN)
	{
		LogDebug("OHCI", "%u: Scheduling Overrun", Controller->Id);

		/* Acknowledge Interrupt */
		Controller->Registers->HcInterruptStatus = OHCI_INTR_SCHEDULING_OVERRUN;
		IntrState = IntrState & ~(OHCI_INTR_SCHEDULING_OVERRUN);
	}

	/* Resume Detection? */
	if (IntrState & OHCI_INTR_RESUMEDETECT)
	{
		/* We must wait 20 ms before putting Controller to Operational */
		DelayMs(20);
		OhciSetMode(Controller, OHCI_CONTROL_ACTIVE);

		/* Acknowledge Interrupt */
		Controller->Registers->HcInterruptStatus = OHCI_INTR_RESUMEDETECT;
		IntrState = IntrState & ~(OHCI_INTR_RESUMEDETECT);
	}

	/* Frame Overflow
	 * Happens when it rolls over from 0xFFFF to 0 */
	if (IntrState & OHCI_INTR_FRAME_OVERFLOW)
	{
		/* Acknowledge Interrupt */
		Controller->Registers->HcInterruptStatus = OHCI_INTR_FRAME_OVERFLOW;
		IntrState = IntrState & ~(OHCI_INTR_FRAME_OVERFLOW);
	}

	/* Why yes, yes it was, wake up the Td handler thread
	* if it was head_done_writeback */
	if (IntrState & OHCI_INTR_PROCESS_HEAD)
	{
		/* Wuhu, handle this! */
		uint32_t TdAddress = (Controller->HCCA->HeadDone & ~(0x00000001));

		/* Call our processor */
		OhciProcessDoneQueue(Controller, TdAddress);

		/* Acknowledge Interrupt */
		Controller->HCCA->HeadDone = 0;
		Controller->Registers->HcInterruptStatus = OHCI_INTR_PROCESS_HEAD;
		IntrState = IntrState & ~(OHCI_INTR_PROCESS_HEAD);
	}

	/* Root Hub Status Change
	* Do a Port Status check */
	if (IntrState & OHCI_INTR_ROOTHUB_EVENT)
	{
		/* Port does not matter here */
		UsbEventCreate(UsbGetHcd(Controller->HcdId), 0, HcdRootHubEvent);

		/* Acknowledge Interrupt */
		Controller->Registers->HcInterruptStatus = OHCI_INTR_ROOTHUB_EVENT;
		IntrState = IntrState & ~(OHCI_INTR_ROOTHUB_EVENT);
	}

	/* Start of Frame? */
	if (IntrState & OHCI_INTR_SOF)
	{
		/* Acknowledge Interrupt */
		Controller->Registers->HcInterruptStatus = OHCI_INTR_SOF;
		IntrState = IntrState & ~(OHCI_INTR_SOF);
	}

	/* Mask out remaining interrupts, we dont use them */
	if (IntrState & ~(OHCI_INTR_MASTER))
		Controller->Registers->HcInterruptDisable = IntrState;

	/* Enable Interrupts */
	Controller->Registers->HcInterruptEnable = (uint32_t)OHCI_INTR_MASTER;

	/* Done! */
	return X86_IRQ_HANDLED;
}