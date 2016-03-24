/* MollenOS
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
* MollenOS USB Periodic Scheduling Code
*/

/* Includes */
#include <UsbCore.h>
#include <UsbScheduler.h>
#include <Heap.h>

/* CLib */
#include <string.h>


/* Initializor 
 * Instantiates a new scheduler with the given size
 * and max-bandwidth per period 
 * MaxBandwidth is usually either 800 or 900 */
UsbScheduler_t *UsbSchedulerInit(size_t Size, size_t MaxBandwidth)
{
	/* Allocate a new instance 
	 * and zero it out */
	UsbScheduler_t *Schedule = (UsbScheduler_t*)kmalloc(sizeof(UsbScheduler_t));
	memset(Schedule, 0, sizeof(UsbScheduler_t));

	/* Set members */
	Schedule->Size = Size;
	Schedule->MaxBandwidth = MaxBandwidth;
	Schedule->MaskSize = 1;
	
	/* Allocate frame list 
	 * and make sure it's zero'd out */
	Schedule->Frames = (size_t*)kmalloc(sizeof(size_t) * Size);
	memset(Schedule->Frames, 0, sizeof(size_t) * Size);

	/* Reset Lock */
	SpinlockReset(&Schedule->Lock);

	/* Done! */
	return Schedule;
}

/* Destructor 
 * Cleans up an usb-schedule */
void UsbSchedulerDestroy(UsbScheduler_t *Schedule)
{
	/* Free the frame list */
	kfree(Schedule->Frames);

	/* Free the base */
	kfree(Schedule);
}

/* Set Mask Size 
 * Only really used for EHCI 
 * due to it's possibily to schedule micro-frames
 * and not only just frames */
void UsbSchedulerSetMaskSize(UsbScheduler_t *Schedule, size_t Size)
{
	/* Store */
	Schedule->MaskSize = Size;
}

/* Calculates bus-time in nano-seconds (approximately) */
long UsbCalculateBandwidth(UsbSpeed_t Speed, int Direction, UsbTransferType_t Type, size_t Length)
{
	/* Vars */
	unsigned long Temp = 0;

	/* Depends on device speed */
	switch (Speed) {
	case LowSpeed:
		if (Direction == USB_EP_DIRECTION_IN) {
			Temp = (67667L * (31L + 10L * BitTime(Length))) / 1000L;
			return 64060L + (2 * BW_HUB_LS_SETUP) + BW_HOST_DELAY + Temp;
		}
		else {
			Temp = (66700L * (31L + 10L * BitTime(Length))) / 1000L;
			return 64107L + (2 * BW_HUB_LS_SETUP) + BW_HOST_DELAY + Temp;
		}
	case FullSpeed:
		if (Type == IsochronousTransfer) {
			Temp = (8354L * (31L + 10L * BitTime(Length))) / 1000L;
			return ((Direction == USB_EP_DIRECTION_IN) ? 7268L : 6265L) + BW_HOST_DELAY + Temp;
		}
		else {
			Temp = (8354L * (31L + 10L * BitTime(Length))) / 1000L;
			return 9107L + BW_HOST_DELAY + Temp;
		}
	case SuperSpeed:
	case HighSpeed:
		if (Type == IsochronousTransfer)
			Temp = HS_NSECS_ISO(Length);
		else
			Temp = HS_NSECS(Length);
	}

	/* Catch */
	return Temp;
}

/* Validate Bandwidth 
 * This function makes sure there is enough 
 * room for the requested bandwidth 
 * Period => Is the actual frequency we want it occuring in ms
 * Bandwidth => Is the NS required for allocation */
int UsbSchedulerValidate(UsbScheduler_t *Schedule, size_t Period, size_t Bandwidth, size_t Mask)
{
	/* Vars */
	size_t i, j;

	/* Sanitize period */
	if (Period == 0) Period = 1;
	if (Period > Schedule->Size) Period = Schedule->Size;

	/* Iterate requested period */
	for (i = 0; i < Schedule->Size; i += (Period * Schedule->MaskSize))
	{
		/* Sanitize initial bandwidth */
		if ((Schedule->Frames[i] + Bandwidth) > (Schedule->MaxBandwidth * Schedule->MaskSize))
			return -1;

		/* For EHCI we must support bandwidth masks 
		 * instead of having a scheduler with multiple
		 * levels, we have it flattened */
		if (Mask != 0
			&& Schedule->MaskSize != 1)
		{
			for (j = 0; i < Schedule->MaskSize; j++) {
				if ((1 << j) & Mask) {
					/* Validate Bandwidth on this mask */
					if ((Schedule->Frames[i + j] + Bandwidth) > Schedule->MaxBandwidth)
						return -1;
				}
			}
		}
	}

	/* Done */
	return 0;
}

/* Reservate Bandwidth 
 * This function actually makes the reservation 
 * Validate Bandwith should have been called first */
int UsbSchedulerReserveBandwidth(UsbScheduler_t *Schedule, size_t Period, size_t Bandwidth, size_t Mask)
{
	/* Vars */
	int RetVal = 0;
	size_t i, j;

	/* Sanitize period */
	if (Period == 0) Period = 1;
	if (Period > Schedule->Size) Period = Schedule->Size;

	/* Request lock */
	SpinlockAcquire(&Schedule->Lock);

	/* Validate (With Lock)! */
	if (UsbSchedulerValidate(Schedule, Period, Bandwidth, Mask)) {
		RetVal = -1;
		goto GoOut;
	}

	/* Iterate requested period */
	for (i = 0; i < Schedule->Size; i += (Period * Schedule->MaskSize))
	{
		/* Allocate outer */
		Schedule->Frames[i] += Bandwidth;

		/* For EHCI we must support bandwidth masks
		 * instead of having a scheduler with multiple
		 * levels, we have it flattened */
		if (Mask != 0
			&& Schedule->MaskSize != 1)
		{
			for (j = 0; i < Schedule->MaskSize; j++) {
				if ((1 << j) & Mask) {
					Schedule->Frames[i + j] += Bandwidth;
				}
			}
		}
	}

GoOut:
	/* Release lock */
	SpinlockRelease(&Schedule->Lock);

	/* Done */
	return RetVal;
}

/* Release Bandwidth */
int UsbSchedulerReleaseBandwidth(UsbScheduler_t *Schedule, size_t Period, size_t Bandwidth, size_t Mask)
{
	/* Vars */
	int RetVal = 0;
	size_t i, j;

	/* Sanitize period */
	if (Period == 0) Period = 1;
	if (Period > Schedule->Size) Period = Schedule->Size;

	/* Request lock */
	SpinlockAcquire(&Schedule->Lock);

	/* Iterate requested period */
	for (i = 0; i < Schedule->Size; i += (Period * Schedule->MaskSize))
	{
		/* Allocate outer */
		Schedule->Frames[i] -= MIN(Bandwidth, Schedule->Frames[i]);

		/* For EHCI we must support bandwidth masks
		* instead of having a scheduler with multiple
		* levels, we have it flattened */
		if (Mask != 0
			&& Schedule->MaskSize != 1)
		{
			for (j = 0; i < Schedule->MaskSize; j++) {
				if ((1 << j) & Mask) {
					Schedule->Frames[i + j] -= MIN(Bandwidth, Schedule->Frames[i + j]);
				}
			}
		}
	}

	/* Release lock */
	SpinlockRelease(&Schedule->Lock);

	/* Done */
	return RetVal;
}