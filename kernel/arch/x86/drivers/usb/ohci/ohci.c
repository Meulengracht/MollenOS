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
	uint32_t val = Controller->Registers->HcControl;
	val = (val & ~X86_OHCI_CTRL_USB_SUSPEND);
	val |= Mode;
	Controller->Registers->HcControl = val;
}

Addr_t OhciAlign(Addr_t addr, Addr_t alignment_bits, Addr_t alignment)
{
	Addr_t aligned_addr = addr;

	if (aligned_addr & alignment_bits)
	{
		aligned_addr &= ~alignment_bits;
		aligned_addr += alignment;
	}

	return aligned_addr;
}

/* Stop/Start */
void OhciStop(OhciController_t *Controller)
{
	uint32_t temp;

	/* Disable BULK and CONTROL queues */
	temp = Controller->Registers->HcControl;
	temp = (temp & ~0x00000030);
	Controller->Registers->HcControl = temp;

	/* Tell Command Status we dont have the list filled */
	temp = Controller->Registers->HcCommandStatus;
	temp = (temp & ~0x00000006);
	Controller->Registers->HcCommandStatus = temp;
}

/* This resets a Port, this is only ever
* called from an interrupt and thus we can't use StallMs :/ */
void OhciPortReset(OhciController_t *Controller, uint32_t Port, int NoInt)
{
	int i = 0;
	uint32_t temp;

	/* Set reset */
	if (NoInt)
		Controller->Registers->HcRhPortStatus[Port] = (X86_OHCI_PORT_RESET | X86_OHCI_PORT_CONNECT_EVENT);
	else
		Controller->Registers->HcRhPortStatus[Port] = (X86_OHCI_PORT_RESET);

	/* Wait with timeout */
	temp = Controller->Registers->HcRhPortStatus[Port];
	while ((temp & X86_OHCI_PORT_RESET)
		&& (i < 1000))
	{
		/* Increase timeout */
		i++;

		/* Stall */
		StallMs(5);

		/* Update */
		temp = Controller->Registers->HcRhPortStatus[Port];
	}

	/* Clear Reset Event */
	if (NoInt)
		Controller->Registers->HcRhPortStatus[Port] = X86_OHCI_PORT_RESET_EVENT;

	/* Set Enable */
	if (!(Controller->Registers->HcRhPortStatus[Port] & X86_OHCI_PORT_ENABLED))
	{
		if (Controller->PowerMode == X86_OHCI_POWER_PORT_CONTROLLED)
			Controller->Registers->HcRhPortStatus[Port] = X86_OHCI_PORT_ENABLED | X86_OHCI_PORT_POWER_ENABLE;
		else
			Controller->Registers->HcRhPortStatus[Port] = X86_OHCI_PORT_ENABLED;
	}

	/* Stall */
	StallMs(100);
}

/* Callbacks */
void OhciPortStatus(void *ControllerData, UsbHcPort_t *Port)
{
	OhciController_t *Controller = (OhciController_t*)ControllerData;
	uint32_t status;

	/* Reset Port */
	OhciPortReset(Controller, Port->Id, 1);

	/* Update information in Port */
	status = Controller->Registers->HcRhPortStatus[Port->Id];

	/* Is it connected? */
	if (status & X86_OHCI_PORT_CONNECTED)
		Port->Connected = 1;
	else
		Port->Connected = 0;

	/* Is it enabled? */
	if (status & X86_OHCI_PORT_ENABLED)
		Port->Enabled = 1;
	else
		Port->Enabled = 0;

	/* Is it full-speed? */
	if (status & X86_OHCI_PORT_LOW_SPEED)
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

	printf("OHCI: Port Status %u: 0x%x\n", Port->Id, status);
}

/* Port Functions */
void OhciPortCheck(OhciController_t *Controller, uint32_t Port)
{
	UsbHc_t *hc;

	/* Was it connect event and not disconnect ? */
	if (Controller->Registers->HcRhPortStatus[Port] & X86_OHCI_PORT_CONNECT_EVENT)
	{
		if (Controller->Registers->HcRhPortStatus[Port] & X86_OHCI_PORT_CONNECTED)
		{
			/* Reset on Attach */
			OhciPortReset(Controller, Port, 0);
		}
		else
		{
			/* Nah, disconnect event */

			/* Get HCD data */
			hc = UsbGetHcd(Controller->HcdId);

			/* Sanity */
			if (hc == NULL)
				return;

			/* Disconnect */
			UsbEventCreate(hc, Port, X86_USB_EVENT_DISCONNECTED);
		}

		/* If Device is enabled, and powered, set it up */
		if ((Controller->Registers->HcRhPortStatus[Port] & X86_OHCI_PORT_ENABLED)
			&& (Controller->Registers->HcRhPortStatus[Port] & X86_OHCI_PORT_POWER_ENABLE))
		{
			/* Get HCD data */
			hc = UsbGetHcd(Controller->HcdId);

			/* Sanity */
			if (hc == NULL)
			{
				printf("OHCI: Controller %u is zombie and is trying to register Ports!!\n", Controller->Id);
				return;
			}

			/* Register Device */
			UsbEventCreate(hc, Port, X86_USB_EVENT_CONNECTED);
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
	Addr_t buffer_address = 0, buffer_address_max = 0;
	Addr_t pool = 0, pool_phys = 0;
	Addr_t ed_level;
	int i;

	/* Initialise ED Pool */
	Controller->EDIndex = 0; 
	for (i = 0; i < X86_OHCI_POOL_NUM_ED; i++)
	{
		Addr_t a_space = (Addr_t)kmalloc(sizeof(OhciEndpointDescriptor_t) + X86_OHCI_STRUCT_ALIGN);
		Controller->EDPool[i] = (OhciEndpointDescriptor_t*)OhciAlign(a_space, X86_OHCI_STRUCT_ALIGN_BITS, X86_OHCI_STRUCT_ALIGN);
		memset((void*)Controller->EDPool[i], 0, sizeof(OhciEndpointDescriptor_t));
		Controller->EDPool[i]->NextED = 0;
		Controller->EDPool[i]->Flags = X86_OHCI_EP_SKIP;
	}

	/* Initialise Bulk/Control TD Pool & Buffers */
	Controller->TDIndex = 0;
	buffer_address = (Addr_t)kmalloc_a(0x1000);
	buffer_address_max = buffer_address + 0x1000 - 1;
	
	pool = (Addr_t)kmalloc((sizeof(OhciGTransferDescriptor_t) * X86_OHCI_POOL_NUM_TD) + X86_OHCI_STRUCT_ALIGN);
	pool = OhciAlign(pool, X86_OHCI_STRUCT_ALIGN_BITS, X86_OHCI_STRUCT_ALIGN);
	pool_phys = MmVirtualGetMapping(NULL, pool);
	memset((void*)pool, 0, sizeof(OhciGTransferDescriptor_t) * X86_OHCI_POOL_NUM_TD);
	for (i = 0; i < X86_OHCI_POOL_NUM_TD; i++)
	{
		/* Set */
		Controller->TDPool[i] = (OhciGTransferDescriptor_t*)pool;
		Controller->TDPoolPhys[i] = pool_phys;

		/* Allocate another page? */
		if (buffer_address > buffer_address_max)
		{
			buffer_address = (Addr_t)kmalloc_a(0x1000);
			buffer_address_max = buffer_address + 0x1000 - 1;
		}

		/* Setup buffer */
		Controller->TDPoolBuffers[i] = (Addr_t*)buffer_address;
		Controller->TDPool[i]->Cbp = MmVirtualGetMapping(NULL, buffer_address);

		/* Increase */
		pool += sizeof(OhciGTransferDescriptor_t);
		pool_phys += sizeof(OhciGTransferDescriptor_t);
		buffer_address += 0x200;
	}

	/* Setup Interrupt Table 
	 * We simply use the DMA
	 * allocation */
	Controller->IntrTable = (OhciIntrTable_t*)(Controller->HccaSpace + 512);

	/* Setup first level */
	ed_level = Controller->HccaSpace + 512;
	ed_level += 16 * sizeof(OhciEndpointDescriptor_t);
	for (i = 0; i < 16; i++)
	{
		Controller->IntrTable->Ms16[i].NextED = ed_level + ((i / 2) * sizeof(OhciEndpointDescriptor_t));
		Controller->IntrTable->Ms16[i].NextEDVirtual = ed_level + ((i / 2) * sizeof(OhciEndpointDescriptor_t));
		Controller->IntrTable->Ms16[i].Flags = X86_OHCI_EP_SKIP;
	}

	/* Second level (8 ms) */
	ed_level += 8 * sizeof(OhciEndpointDescriptor_t);
	for (i = 0; i < 8; i++)
	{
		Controller->IntrTable->Ms8[i].NextED = ed_level + ((i / 2) * sizeof(OhciEndpointDescriptor_t));
		Controller->IntrTable->Ms8[i].NextEDVirtual = ed_level + ((i / 2) * sizeof(OhciEndpointDescriptor_t));
		Controller->IntrTable->Ms8[i].Flags = X86_OHCI_EP_SKIP;
	}

	/* Third level (4 ms) */
	ed_level += 4 * sizeof(OhciEndpointDescriptor_t);
	for (i = 0; i < 4; i++)
	{
		Controller->IntrTable->Ms4[i].NextED = ed_level + ((i / 2) * sizeof(OhciEndpointDescriptor_t));
		Controller->IntrTable->Ms4[i].NextEDVirtual = ed_level + ((i / 2) * sizeof(OhciEndpointDescriptor_t));
		Controller->IntrTable->Ms4[i].Flags = X86_OHCI_EP_SKIP;
	}

	/* Fourth level (2 ms) */
	ed_level += 2 * sizeof(OhciEndpointDescriptor_t);
	for (i = 0; i < 2; i++)
	{
		Controller->IntrTable->Ms2[i].NextED = ed_level + sizeof(OhciEndpointDescriptor_t);
		Controller->IntrTable->Ms2[i].NextEDVirtual = ed_level + sizeof(OhciEndpointDescriptor_t);
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

	/* Allocate a transaction list */
	Controller->TransactionsWaitingBulk = 0;
	Controller->TransactionsWaitingControl = 0;
	Controller->TransactionQueueBulk = 0;
	Controller->TransactionQueueControl = 0;
	Controller->TransactionList = list_create(LIST_SAFE);
}

/* Resets the controllre to a working state from initial */
void OhciSetup(OhciController_t *Controller)
{
	UsbHc_t *hc;
	uint32_t temp_value = 0, temp = 0, fmint = 0;
	int i;

	/* Step 1. Verify the Revision */
	temp_value = (Controller->Registers->HcRevision & 0xFF);
	if (temp_value != X86_OHCI_REVISION)
	{
		printf("OHCI Revision is wrong (0x%x), exiting :(\n", temp_value);
		MmPhysicalFreeBlock(Controller->HccaSpace);
		kfree(Controller);
		return;
	}

	/* Step 2. Init Virtual Queues */
	OhciInitQueues(Controller);

	/* Step 3. Gain control of Controller */

	/* Is SMM the bitch? */
	if (Controller->Registers->HcControl & X86_OHCI_CTRL_INT_ROUTING)
	{
		/* Ok, SMM has control, now give me my hat back */
		temp = Controller->Registers->HcCommandStatus;
		temp |= X86_OHCI_CMD_OWNERSHIP;
		Controller->Registers->HcCommandStatus = temp;

		/* Wait for InterruptRouting to clear */
		i = 0;
		while ((i < 500) 
			&& (Controller->Registers->HcControl & X86_OHCI_CTRL_INT_ROUTING))
		{
			/* Idle idle idle */
			StallMs(10);

			/* Increase I */
			i++;
		}

		if (i == 500)
		{
			/* Did not work, reset bit, try that */
			Controller->Registers->HcControl &= ~X86_OHCI_CTRL_INT_ROUTING;
			StallMs(200);

			if (Controller->Registers->HcControl & X86_OHCI_CTRL_INT_ROUTING)
			{
				printf("OHCI: SMM Won't give us the Controller, we're backing down >(\n");
				MmPhysicalFreeBlock(Controller->HccaSpace);
				kfree(Controller);
				return;
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

	/* Disable All Interrupts */
	Controller->Registers->HcInterruptDisable = (uint32_t)X86_OHCI_INTR_MASTER_INTR;

	/* Perform a reset of HC Controller */
	OhciSetMode(Controller, X86_OHCI_CTRL_USB_SUSPEND);
	StallMs(200);

	/* Okiiii, reset Controller, we need to save FmInterval */
	fmint = Controller->Registers->HcFmInterval;

	/* Set bit 0 to Request reboot */
	temp = Controller->Registers->HcCommandStatus;
	temp |= X86_OHCI_CMD_RESETCTRL;
	Controller->Registers->HcCommandStatus = temp;

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
	temp_value = (Controller->Registers->HcFmInterval & 0x3FFF);
	Controller->Registers->HcPeriodicStart = (temp_value / 10) * 9;

	/* Setup Control */
	temp = Controller->Registers->HcControl;
	if (temp & X86_OHCI_CTRL_REMOTE_WAKE)
		temp |= X86_OHCI_CTRL_REMOTE_WAKE;

	/* Clear Lists, Mode, Ratio and IR */
	temp = (temp & ~(0x0000003C | X86_OHCI_CTRL_USB_SUSPEND | 0x3 | 0x100));

	/* Set Ratio (4:1) and Mode (Operational) */
	temp |= (0x3 | X86_OHCI_CTRL_USB_WORKING);
	Controller->Registers->HcControl = temp;

	/* Now restore FmInterval */
	Controller->Registers->HcFmInterval = fmint;

	/* Controller is now running! */
	printf("OHCI: Controller %u Started, Control 0x%x, Ints 0x%x\n",
		Controller->Id, Controller->Registers->HcControl, Controller->Registers->HcInterruptEnable);

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
	temp_value = Controller->Registers->HcRhDescriptorA;
	temp_value >>= 24;
	temp_value &= 0x000000FF;
	temp_value *= 2;

	/* Give it atleast 100 ms :p */
	if (temp_value < 100)
		temp_value = 100;

	Controller->PowerOnDelayMs = temp_value;

	printf("OHCI: Ports %u (power Mode %u, power delay %u)\n", 
		Controller->Ports, Controller->PowerMode, temp_value);

	/* Setup HCD */
	hc = UsbInitController((void*)Controller, X86_USB_TYPE_OHCI, Controller->Ports);

	/* Port Functions */
	hc->RootHubCheck = OhciPortsCheck;
	hc->PortSetup = OhciPortStatus;

	/* Transaction Functions */
	hc->TransactionInit = OhciTransactionInit;
	hc->TransactionSetup = OhciTransactionSetup;
	hc->TransactionIn = OhciTransactionIn;
	hc->TransactionOut = OhciTransactionOut;
	hc->TransactionSend = OhciTransactionSend;
	hc->InstallInterrupt = OhciInstallInterrupt;

	Controller->HcdId = UsbRegisterController(hc);

	/* Setup Ports */
	for (i = 0; i < (int)Controller->Ports; i++)
	{
//		int p = i;

		/* Make sure power is on */
		if (!(Controller->Registers->HcRhPortStatus[i] & X86_OHCI_PORT_POWER_ENABLE))
		{
			/* Powerup! */
			Controller->Registers->HcRhPortStatus[i] = X86_OHCI_PORT_POWER_ENABLE;

			/* Wait for power to stabilize */
			StallMs(Controller->PowerOnDelayMs);
		}

		/* Check if Port is connected */
		//if (Controller->Registers->HcRhPortStatus[i] & X86_OHCI_PORT_CONNECTED)
			//UsbEventCreate(UsbGetHcd(Controller->HcdId), p, X86_USB_EVENT_CONNECTED);
	}

	/* Now we can enable hub events (and clear interrupts) */
	Controller->Registers->HcInterruptStatus &= ~(uint32_t)0;
	Controller->Registers->HcInterruptEnable = X86_OHCI_INTR_ROOT_HUB_EVENT;
}

/* Reset Controller */
void OhciReset(OhciController_t *Controller)
{
	uint32_t temp_value, temp, fmint;
	int i;

	/* Disable All Interrupts */
	Controller->Registers->HcInterruptDisable = (uint32_t)X86_OHCI_INTR_MASTER_INTR;

	/* Perform a reset of HC Controller */
	OhciSetMode(Controller, X86_OHCI_CTRL_USB_SUSPEND);
	StallMs(200);

	/* Okiiii, reset Controller, we need to save FmInterval */
	fmint = Controller->Registers->HcFmInterval;

	/* Set bit 0 to Request reboot */
	temp = Controller->Registers->HcCommandStatus;
	temp |= X86_OHCI_CMD_RESETCTRL;
	Controller->Registers->HcCommandStatus = temp;

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
	temp_value = (Controller->Registers->HcFmInterval & 0x3FFF);
	Controller->Registers->HcPeriodicStart = (temp_value / 10) * 9;

	/* Setup Control */
	temp = Controller->Registers->HcControl;
	if (temp & X86_OHCI_CTRL_REMOTE_WAKE)
		temp |= X86_OHCI_CTRL_REMOTE_WAKE;

	/* Clear Lists, Mode, Ratio and IR */
	temp = (temp & ~(0x0000003C | X86_OHCI_CTRL_USB_SUSPEND | 0x3 | 0x100));

	/* Set Ratio (4:1) and Mode (Operational) */
	temp |= (0x3 | X86_OHCI_CTRL_USB_WORKING);
	Controller->Registers->HcControl = temp;

	/* Now restore FmInterval */
	Controller->Registers->HcFmInterval = fmint;

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
uint32_t OhciAllocateEp(OhciController_t *Controller)
{
	int32_t index = -1;
	OhciEndpointDescriptor_t *ed;

	/* Pick a QH */
	SpinlockAcquire(&Controller->Lock);

	/* Grap it, locked operation */
	while (index == -1)
	{
		ed = Controller->EDPool[Controller->EDIndex];

		if (ed->Flags & X86_OHCI_EP_SKIP)
		{
			/* Done! */
			index = Controller->EDIndex;
			ed->Flags = 0;
		}

		Controller->EDIndex++;

		/* Index Sanity */
		if (Controller->EDIndex == X86_OHCI_POOL_NUM_ED)
			Controller->EDIndex = 0;
	}

	/* Release Lock */
	SpinlockRelease(&Controller->Lock);

	return (uint32_t)index;
}

void OhciEpInit(OhciEndpointDescriptor_t *ep, Addr_t FirstTd, uint32_t Type, 
	uint32_t Address, uint32_t Endpoint, uint32_t PacketSize, uint32_t LowSpeed)
{
	/* Setup Flags 
	 * HighSpeed Bulk/Control/Interrupt */
	ep->Flags = 0;
	ep->Flags |= (Address & X86_OHCI_EP_ADDR_BITS);
	ep->Flags |= X86_OHCI_EP_EP_NUM((Endpoint & X86_OHCI_EP_EP_NUM_BITS));
	ep->Flags |= X86_OHCI_EP_LOWSPEED(LowSpeed);
	ep->Flags |= X86_OHCI_EP_PID_TD; /* Get PID from TD */
	ep->Flags |= X86_OHCI_EP_PACKET_SIZE((PacketSize & X86_OHCI_EP_PACKET_BITS));
	ep->Flags |= X86_OHCI_EP_TYPE((Type & 0xF));

	/* Set TD */
	if (FirstTd == X86_OHCI_TRANSFER_END_OF_LIST)
		ep->HeadPtr = X86_OHCI_TRANSFER_END_OF_LIST;
	else
		ep->HeadPtr = MmVirtualGetMapping(NULL, (FirstTd & ~0xD));

	/* Set Tail */
	ep->NextED = 0;
	ep->NextEDVirtual = 0;
}

/* TD Functions */
uint32_t OhciAllocateTd(OhciController_t *Controller)
{
	int32_t index = -1;
	OhciGTransferDescriptor_t *td;

	/* Pick a QH */
	SpinlockAcquire(&Controller->Lock);

	/* Grap it, locked operation */
	while (index == -1)
	{
		td = Controller->TDPool[Controller->TDIndex];

		if (!(td->Flags & X86_OHCI_TRANSFER_BUF_NOCC))
		{
			/* Done! */
			index = Controller->TDIndex;
			td->Flags |= X86_OHCI_TRANSFER_BUF_NOCC;
		}

		Controller->TDIndex++;

		/* Index Sanity */
		if (Controller->TDIndex == X86_OHCI_POOL_NUM_TD)
			Controller->TDIndex = 0;
	}

	/* Release Lock */
	SpinlockRelease(&Controller->Lock);

	return (uint32_t)index;
}

OhciGTransferDescriptor_t *OhciTdSetup(OhciController_t *Controller, 
	OhciEndpointDescriptor_t *ed, Addr_t NextTD, uint32_t Toggle, uint8_t RequestDirection,
	uint8_t RequestType, uint8_t RequestValueLo, uint8_t RequestValueHi, uint16_t RequestIndex,
	uint16_t RequestLength, void **TDBuffer)
{
	UsbPacket_t *packet;
	OhciGTransferDescriptor_t *td;
	Addr_t td_phys;
	void *buffer;
	uint32_t TDIndex;

	/* Allocate a TD */
	TDIndex = OhciAllocateTd(Controller);

	/* Grab a TD and a buffer */
	td = Controller->TDPool[TDIndex];
	buffer = Controller->TDPoolBuffers[TDIndex];
	td_phys = Controller->TDPoolPhys[TDIndex];
	
	/* EOL ? */
	if (NextTD == X86_OHCI_TRANSFER_END_OF_LIST)
		td->NextTD = X86_OHCI_TRANSFER_END_OF_LIST;
	else	/* Get physical Address of NextTD and set NextTD to that */
		td->NextTD = MmVirtualGetMapping(NULL, (VirtAddr_t)NextTD); 

	/* Setup the TD for a SETUP TD */
	td->Flags = 0;
	td->Flags |= X86_OHCI_TRANSFER_BUF_ROUNDING;
	td->Flags |= X86_OHCI_TRANSFER_BUF_PID_SETUP;
	td->Flags |= (Toggle << 24);
	td->Flags |= X86_OHCI_TRANSFER_BUF_TD_TOGGLE;
	td->Flags |= X86_OHCI_TRANSFER_BUF_NOCC;

	/* Setup the SETUP Request */
	*TDBuffer = buffer;
	packet = (UsbPacket_t*)buffer;
	packet->Direction = RequestDirection;
	packet->Type = RequestType;
	packet->ValueLo = RequestValueLo;
	packet->ValueHi = RequestValueHi;
	packet->Index = RequestIndex;
	packet->Length = RequestLength;

	/* Set TD buffer */
	td->Cbp = MmVirtualGetMapping(NULL, (VirtAddr_t)buffer);
	td->BufferEnd = td->Cbp + sizeof(UsbPacket_t) - 1;

	/* Make Queue Tail point to this */
	ed->TailPtr = td_phys;

	return td;
}

OhciGTransferDescriptor_t *ohci_td_io(OhciController_t *Controller,
	OhciEndpointDescriptor_t *ed, Addr_t NextTD, uint32_t Toggle, uint32_t pid, 
	uint32_t Length, void **TDBuffer)
{
	OhciGTransferDescriptor_t *td;
	Addr_t td_phys;
	void *buffer;
	uint32_t TDIndex;

	/* Allocate a TD */
	TDIndex = OhciAllocateTd(Controller);

	/* Grab a TD and a buffer */
	td = Controller->TDPool[TDIndex];
	buffer = Controller->TDPoolBuffers[TDIndex];
	td_phys = Controller->TDPoolPhys[TDIndex];

	/* EOL ? */
	if (NextTD == X86_OHCI_TRANSFER_END_OF_LIST)
		td->NextTD = X86_OHCI_TRANSFER_END_OF_LIST;
	else	/* Get physical Address of NextTD and set NextTD to that */
		td->NextTD = MmVirtualGetMapping(NULL, (VirtAddr_t)NextTD);

	/* Setup the TD for a IO TD */
	td->Flags = 0;
	td->Flags |= X86_OHCI_TRANSFER_BUF_ROUNDING;
	td->Flags |= pid;
	//td->Flags |= X86_OHCI_TRANSFER_BUF_NO_INTERRUPT;	/* We don't want interrupt */
	td->Flags |= X86_OHCI_TRANSFER_BUF_TD_TOGGLE;
	td->Flags |= X86_OHCI_TRANSFER_BUF_NOCC;
	td->Flags |= (Toggle << 24);

	*TDBuffer = buffer;
	
	/* Bytes to transfer?? */
	if (Length > 0)
	{
		td->Cbp = MmVirtualGetMapping(NULL, (VirtAddr_t)buffer);
		td->BufferEnd = td->Cbp + Length - 1;
	}
	else
	{
		td->Cbp = 0;
		td->BufferEnd = td->Cbp;
	}
		
	
	/* Make Queue Tail point to this */
	ed->TailPtr = td_phys;

	return td;
}

/* Transaction Functions */

/* This one prepaires an ED */
void OhciTransactionInit(void *Controller, UsbHcRequest_t *Request)
{
	OhciController_t *ctrl = (OhciController_t*)Controller;
	uint32_t temp;

	temp = OhciAllocateEp(ctrl);
	Request->Data = ctrl->EDPool[temp];

	/* Set as not completed for start */
	Request->Completed = 0;
}

/* This one prepaires an setup TD */
UsbHcTransaction_t *OhciTransactionSetup(void *Controller, UsbHcRequest_t *Request)
{
	OhciController_t *ctrl = (OhciController_t*)Controller;
	UsbHcTransaction_t *transaction;

	/* Allocate transaction */
	transaction = (UsbHcTransaction_t*)kmalloc(sizeof(UsbHcTransaction_t));
	transaction->IoBuffer = 0;
	transaction->IoLength = 0;
	transaction->Link = NULL;

	/* Create the td */
	transaction->TransferDescriptor = (void*)OhciTdSetup(ctrl, 
		(OhciEndpointDescriptor_t*)Request->Data, X86_OHCI_TRANSFER_END_OF_LIST, 
		Request->Toggle, Request->Packet.Direction, Request->Packet.Type, 
		Request->Packet.ValueLo, Request->Packet.ValueHi, Request->Packet.Index, 
		Request->Packet.Length, &transaction->TransferBuffer);

	/* If previous transaction */
	if (Request->Transactions != NULL)
	{
		OhciGTransferDescriptor_t *prev_td;
		UsbHcTransaction_t *ctrans = Request->Transactions;

		while (ctrans->Link)
			ctrans = ctrans->Link;
		
		prev_td = (OhciGTransferDescriptor_t*)ctrans->TransferDescriptor;
		prev_td->NextTD = MmVirtualGetMapping(NULL, (VirtAddr_t)transaction->TransferDescriptor);
	}

	return transaction;
}

/* This one prepaires an in TD */
UsbHcTransaction_t *OhciTransactionIn(void *Controller, UsbHcRequest_t *Request)
{
	OhciController_t *ctrl = (OhciController_t*)Controller;
	UsbHcTransaction_t *transaction;

	/* Allocate transaction */
	transaction = (UsbHcTransaction_t*)kmalloc(sizeof(UsbHcTransaction_t));
	transaction->IoBuffer = Request->IoBuffer;
	transaction->IoLength = Request->IoLength;
	transaction->Link = NULL;

	/* Setup TD */
	transaction->TransferDescriptor = (void*)ohci_td_io(ctrl,
		(OhciEndpointDescriptor_t*)Request->Data, X86_OHCI_TRANSFER_END_OF_LIST, 
		Request->Toggle, X86_OHCI_TRANSFER_BUF_PID_IN, Request->IoLength, 
		&transaction->TransferBuffer);

	/* If previous transaction */
	if (Request->Transactions != NULL)
	{
		OhciGTransferDescriptor_t *prev_td;
		UsbHcTransaction_t *ctrans = Request->Transactions;

		while (ctrans->Link)
			ctrans = ctrans->Link;

		prev_td = (OhciGTransferDescriptor_t*)ctrans->TransferDescriptor;
		prev_td->NextTD = MmVirtualGetMapping(NULL, (VirtAddr_t)transaction->TransferDescriptor);
	}

	return transaction;
}

/* This one prepaires an out TD */
UsbHcTransaction_t *OhciTransactionOut(void *Controller, UsbHcRequest_t *Request)
{
	OhciController_t *ctrl = (OhciController_t*)Controller;
	UsbHcTransaction_t *transaction;

	/* Allocate transaction */
	transaction = (UsbHcTransaction_t*)kmalloc(sizeof(UsbHcTransaction_t));
	transaction->IoBuffer = 0;
	transaction->IoLength = 0;
	transaction->Link = NULL;

	/* Setup TD */
	transaction->TransferDescriptor = (void*)ohci_td_io(ctrl,
		(OhciEndpointDescriptor_t*)Request->Data, X86_OHCI_TRANSFER_END_OF_LIST,
		Request->Toggle, X86_OHCI_TRANSFER_BUF_PID_OUT, Request->IoLength,
		&transaction->TransferBuffer);

	/* Copy Data */
	if (Request->IoBuffer != NULL && Request->IoLength != 0)
		memcpy(transaction->TransferBuffer, Request->IoBuffer, Request->IoLength);

	/* If previous transaction */
	if (Request->Transactions != NULL)
	{
		OhciGTransferDescriptor_t *prev_td;
		UsbHcTransaction_t *ctrans = Request->Transactions;

		while (ctrans->Link)
			ctrans = ctrans->Link;

		prev_td = (OhciGTransferDescriptor_t*)ctrans->TransferDescriptor;
		prev_td->NextTD = MmVirtualGetMapping(NULL, (VirtAddr_t)transaction->TransferDescriptor);
	}

	return transaction;
}

/* This one queues the transaction up for processing */
void OhciTransactionSend(void *Controller, UsbHcRequest_t *Request)
{
	/* Wuhu */
	UsbHcTransaction_t *transaction = Request->Transactions;
	OhciController_t *ctrl = (OhciController_t*)Controller;
	int completed = 1;
	OhciGTransferDescriptor_t *td = NULL;
	uint32_t condition_code;
	Addr_t ed_address;

	/* Get physical */
	ed_address = MmVirtualGetMapping(NULL, (VirtAddr_t)Request->Data);

	/* Set as not completed for start */
	Request->Completed = 0;

	/* Add dummy TD to end */
	UsbTransactionOut(UsbGetHcd(ctrl->HcdId), Request, 1, 0, 0);

	/* Setup an ED for this */
	((OhciEndpointDescriptor_t*)Request->Data)->HcdData = (uint32_t)transaction;
	OhciEpInit(Request->Data, (Addr_t)Request->Transactions->TransferDescriptor, Request->Type,
		Request->Device->Address, Request->Endpoint, Request->Length, Request->LowSpeed);

	/* Now lets try the transaction */
	SpinlockAcquire(&ctrl->Lock);

	/* Set true */
	completed = 1;

	/* Add this transaction to list */
	((OhciEndpointDescriptor_t*)Request->Data)->HeadPtr &= ~(0x00000001);
	list_append(ctrl->TransactionList, list_create_node(0, Request->Data));

	/* Is this the "first" control transfer? */
	if (Request->Type == X86_USB_REQUEST_TYPE_CONTROL)
	{
		if (ctrl->TransactionsWaitingControl > 0)
		{
			/* Insert it */
			if (ctrl->TransactionQueueControl == 0)
				ctrl->TransactionQueueControl = (uint32_t)Request->Data;
			else
			{
				OhciEndpointDescriptor_t *ep = (OhciEndpointDescriptor_t*)ctrl->TransactionQueueControl;

				/* Find tail */
				while (ep->NextED)
					ep = (OhciEndpointDescriptor_t*)ep->NextEDVirtual;

				/* Insert it */
				ep->NextED = ed_address;
				ep->NextEDVirtual = (uint32_t)Request->Data;
			}

			/* Increase */
			ctrl->TransactionsWaitingControl++;

			/* Release spinlock */
			SpinlockRelease(&ctrl->Lock);
		}
		else
		{
			/* Add it HcControl/BulkCurrentED */
			ctrl->Registers->HcControlHeadED = 
				ctrl->Registers->HcControlCurrentED = ed_address;

			/* Increase */
			ctrl->TransactionsWaitingControl++;

			/* Release spinlock */
			SpinlockRelease(&ctrl->Lock);

			/* Set Lists Filled (Enable Them) */
			ctrl->Registers->HcCommandStatus |= X86_OHCI_CMD_TDACTIVE_CTRL;
			ctrl->Registers->HcControl |= X86_OHCI_CTRL_CONTROL_LIST;
		}
	}
	else if (Request->Type == X86_USB_REQUEST_TYPE_BULK)
	{
		if (ctrl->TransactionsWaitingBulk > 0)
		{
			/* Insert it */
			if (ctrl->TransactionQueueBulk == 0)
				ctrl->TransactionQueueBulk = (Addr_t)Request->Data;
			else
			{
				OhciEndpointDescriptor_t *ep = (OhciEndpointDescriptor_t*)ctrl->TransactionQueueBulk;

				/* Find tail */
				while (ep->NextED)
					ep = (OhciEndpointDescriptor_t*)ep->NextEDVirtual;

				/* Insert it */
				ep->NextED = ed_address;
				ep->NextEDVirtual = (uint32_t)Request->Data;
			}

			/* Increase */
			ctrl->TransactionsWaitingBulk++;
			
			/* Release spinlock */
			SpinlockRelease(&ctrl->Lock);
		}
		else
		{
			/* Add it HcControl/BulkCurrentED */
			ctrl->Registers->HcBulkHeadED = 
				ctrl->Registers->HcBulkCurrentED = ed_address;

			/* Increase */
			ctrl->TransactionsWaitingBulk++;

			/* Release spinlock */
			SpinlockRelease(&ctrl->Lock);

			/* Set Lists Filled (Enable Them) */
			ctrl->Registers->HcCommandStatus |= X86_OHCI_CMD_TDACTIVE_BULK;
			ctrl->Registers->HcControl |= X86_OHCI_CTRL_BULK_LIST;
		}
	}
	
	/* Wait for interrupt */
	SchedulerSleepThread((Addr_t*)Request->Data);
	_yield();

	/* Check Conditions (WithOUT dummy) */
	transaction = Request->Transactions;
	while (transaction->Link)
	{
		td = (OhciGTransferDescriptor_t*)transaction->TransferDescriptor;
		condition_code = (td->Flags & 0xF0000000) >> 28;
		//printf("TD Flags 0x%x, TD Condition Code %u (%s)\n", td->Flags, condition_code, OhciErrorMessages[condition_code]);

		if (condition_code == 0 && completed == 1)
			completed = 1;
		else
			completed = 0;

		transaction = transaction->Link;
	}

	/* Lets see... */
	if (completed)
	{
		/* Build Buffer */
		transaction = Request->Transactions;

		while (transaction->Link)
		{
			/* Copy Data? */
			if (transaction->IoBuffer != NULL && transaction->IoLength != 0)
			{
				//printf("Buffer Copy 0x%x, Length 0x%x\n", transaction->IoBuffer, transaction->IoLength);
				memcpy(transaction->IoBuffer, transaction->TransferBuffer, transaction->IoLength);
			}
			
			/* Next Link */
			transaction = transaction->Link;
		}

		/* Set as completed */
		Request->Completed = 1;
	}
}

/* Install an Interrupt Endpoint */
void OhciInstallInterrupt(void *Controller, UsbHcDevice_t *Device, UsbHcEndpoint_t *Endpoint,
	void *InBuffer, size_t InBytes, void(*Callback)(void*, size_t), void *Arg)
{
	UsbHc_t *hcd = Device->HcDriver;
	OhciController_t *ctrl = (OhciController_t*)Controller;
	OhciEndpointDescriptor_t *ep = NULL, *iep = NULL;
	OhciGTransferDescriptor_t *td = NULL;
	void *TDBuffer = NULL;
	uint32_t period = 32;
	uint32_t i;
	uint32_t LowSpeed = (hcd->Ports[Device->Port]->FullSpeed == 1) ? 0 : 1;
	OhciPeridoicCallback_t *cb_info = (OhciPeridoicCallback_t*)kmalloc(sizeof(OhciPeridoicCallback_t));

	/* Calculate period */
	for (; (period >= Endpoint->Interval) && (period > 0);)
		period >>= 1;

	/* Grab an EP */
	i = OhciAllocateEp(ctrl);
	ep = ctrl->EDPool[i];

	/* Get TD(s) */
	i = OhciAllocateTd(ctrl);
	td = ctrl->TDPool[i];
	TDBuffer = ctrl->TDPoolBuffers[i];

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

	/* Setup TD */
	td->Flags = 0;
	td->Flags |= X86_OHCI_TRANSFER_BUF_ROUNDING;
	td->Flags |= X86_OHCI_TRANSFER_BUF_PID_IN;
	td->Flags |= ((Endpoint->Toggle & 0x1) << 24);
	td->Flags |= X86_OHCI_TRANSFER_BUF_TD_TOGGLE;
	td->Flags |= X86_OHCI_TRANSFER_BUF_NOCC;

	td->NextTD = 0x1;

	td->Cbp = MmVirtualGetMapping(NULL, (VirtAddr_t)TDBuffer);
	td->BufferEnd = td->Cbp + 0x200 - 1;

	/* Setup EP */
	ep->HeadPtr = MmVirtualGetMapping(NULL, (VirtAddr_t)td);
	ep->TailPtr = 0;
	ep->HcdData = (uint32_t)cb_info;

	ep->Flags = (Device->Address & X86_OHCI_EP_ADDR_BITS); /* Device Address */
	ep->Flags |= X86_OHCI_EP_EP_NUM((Endpoint->Address & X86_OHCI_EP_EP_NUM_BITS));
	ep->Flags |= X86_OHCI_EP_LOWSPEED(LowSpeed); /* Device Speed */
	ep->Flags |= X86_OHCI_EP_PACKET_SIZE((Endpoint->MaxPacketSize & X86_OHCI_EP_PACKET_BITS));
	ep->Flags |= X86_OHCI_EP_TYPE(2);

	/* Add transfer */
	list_append(ctrl->TransactionList, list_create_node(0, ep));

	/* Ok, queue it
	 * Lock & stop ints */
	SpinlockAcquire(&ctrl->Lock);

	if (period == 1)
	{
		iep = &ctrl->IntrTable->Ms1[0];

		/* Insert it */
		ep->NextEDVirtual = iep->NextEDVirtual;
		ep->NextED = iep->NextED;
		iep->NextED = MmVirtualGetMapping(NULL, (VirtAddr_t)ep);
		iep->NextEDVirtual = (uint32_t)ep;
	}
	else if (period == 2)
	{
		iep = &ctrl->IntrTable->Ms2[ctrl->I2];

		/* Insert it */
		ep->NextEDVirtual = iep->NextEDVirtual;
		ep->NextED = iep->NextED;
		iep->NextED = MmVirtualGetMapping(NULL, (VirtAddr_t)ep);
		iep->NextEDVirtual = (uint32_t)ep;

		/* Increase i2 */
		ctrl->I2++;
		if (ctrl->I2 == 2)
			ctrl->I2 = 0;
	}
	else if (period == 4)
	{
		iep = &ctrl->IntrTable->Ms4[ctrl->I4];

		/* Insert it */
		ep->NextEDVirtual = iep->NextEDVirtual;
		ep->NextED = iep->NextED;
		iep->NextED = MmVirtualGetMapping(NULL, (VirtAddr_t)ep);
		iep->NextEDVirtual = (uint32_t)ep;

		/* Increase i4 */
		ctrl->I4++;
		if (ctrl->I4 == 4)
			ctrl->I4 = 0;
	}
	else if (period == 8)
	{
		iep = &ctrl->IntrTable->Ms8[ctrl->I8];

		/* Insert it */
		ep->NextEDVirtual = iep->NextEDVirtual;
		ep->NextED = iep->NextED;
		iep->NextED = MmVirtualGetMapping(NULL, (VirtAddr_t)ep);
		iep->NextEDVirtual = (uint32_t)ep;

		/* Increase i8 */
		ctrl->I8++;
		if (ctrl->I8 == 8)
			ctrl->I8 = 0;
	}
	else if (period == 16)
	{
		iep = &ctrl->IntrTable->Ms16[ctrl->I16];

		/* Insert it */
		ep->NextEDVirtual = iep->NextEDVirtual;
		ep->NextED = iep->NextED;
		iep->NextED = MmVirtualGetMapping(NULL, (VirtAddr_t)ep);
		iep->NextEDVirtual = (uint32_t)ep;

		/* Increase i16 */
		ctrl->I16++;
		if (ctrl->I16 == 16)
			ctrl->I16 = 0;
	}
	else
	{
		/* 32 */
		iep = ctrl->ED32[ctrl->I32];

		/* Insert it */
		ep->NextEDVirtual = (Addr_t)iep;
		ep->NextED = MmVirtualGetMapping(NULL, (VirtAddr_t)iep);

		/* Make int-table point to this */
		ctrl->HCCA->InterruptTable[ctrl->I32] = MmVirtualGetMapping(NULL, (VirtAddr_t)ep);
		ctrl->ED32[ctrl->I32] = ep;

		/* Increase i32 */
		ctrl->I32++;
		if (ctrl->I32 == 32)
			ctrl->I32 = 0;
	}

	/* Done */
	SpinlockRelease(&ctrl->Lock);

	/* Enable Queue in case it was disabled */
	ctrl->Registers->HcControl |= X86_OCHI_CTRL_PERIODIC_LIST;
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
		OhciEndpointDescriptor_t *ep = (OhciEndpointDescriptor_t*)ta->data;
		transfer_type = (ep->Flags >> 27);

		/* Special Case */
		if (transfer_type == 2)
		{
			/* Interrupt */
			OhciPeridoicCallback_t *cb_info = (OhciPeridoicCallback_t*)ep->HcdData;
			
			/* Is it this? */
			if (Controller->TDPoolPhys[cb_info->TDIndex] == done_head)
			{
				/* Yeps */
				void *TDBuffer = Controller->TDPoolBuffers[cb_info->TDIndex];
				OhciGTransferDescriptor_t *interrupt_td = Controller->TDPool[cb_info->TDIndex];
				uint32_t condition_code = (interrupt_td->Flags & 0xF0000000) >> 28;

				/* Sanity */
				if (condition_code == 0)
				{
					/* Get data */
					memcpy(cb_info->Buffer, TDBuffer, cb_info->Bytes);

					/* Inform Callback */
					cb_info->Callback(cb_info->Args, cb_info->Bytes);
				}

				/* Restart TD */
				interrupt_td->Cbp = interrupt_td->BufferEnd - 0x200 + 1;
				interrupt_td->Flags |= X86_OHCI_TRANSFER_BUF_NOCC;
				
				/* Toggle TD D bit!??!??! */

				/* Reset EP */
				ep->HeadPtr = Controller->TDPoolPhys[cb_info->TDIndex];

				/* We are done, return */
				return;
			}
		}
		else
		{
			UsbHcTransaction_t *t_list = (UsbHcTransaction_t*)ep->HcdData;

			/* Process TD's and see if all transfers are done */
			while (t_list)
			{
				/* Get physical of TD */
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
						OhciGTransferDescriptor_t *td = (OhciGTransferDescriptor_t*)t_list->TransferDescriptor;
						uint32_t condition_code = (td->Flags & 0xF0000000) >> 28;
						printf("FAILURE: TD Flags 0x%x, TD Condition Code %u (%s)\n", td->Flags, condition_code, OhciErrorMessages[condition_code]);
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
				/* Get physical of EP */
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
				/* Get physical of EP */
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

		/* Mark EP Descriptor as sKip SEHR IMPORTANTE */
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
	printf("OHCI: Controller %u Interrupt: 0x%x\n", Controller->HcdId, intr_state);

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

	/* Why yes, yes it was, wake up the TD handler thread
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
	 * Do a Port status check */
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
