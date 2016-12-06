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
#include <stddef.h>
#include <string.h>


/* Initializor 
 * Instantiates a new scheduler with the given size
 * and max-bandwidth per period 
 * MaxBandwidth is usually either 800 or 900 */
UsbScheduler_t *UsbSchedulerInit(size_t Size, size_t MaxBandwidth, size_t MaskSize)
{
	/* Allocate a new instance 
	 * and zero it out */
	UsbScheduler_t *Schedule = (UsbScheduler_t*)kmalloc(sizeof(UsbScheduler_t));
	memset(Schedule, 0, sizeof(UsbScheduler_t));

	/* Sanity, if mask size is not 1 
	 * we must allow for even more 
	 * to allow both an 'overview' and their submembers */
	if (MaskSize != 1)
		MaskSize++;

	/* Set members */
	Schedule->Size = Size * MaskSize;
	Schedule->MaskSize = MaskSize;
	Schedule->MaxBandwidth = MaxBandwidth;

	/* Sanity */
	if (MaskSize != 1)
		Schedule->MaxMaskBandwidth = (MaxBandwidth * (MaskSize - 1));
	else
		Schedule->MaxMaskBandwidth = MaxBandwidth;
	
	/* Allocate frame list 
	 * and make sure it's zero'd out */
	Schedule->Frames = (size_t*)kmalloc(sizeof(size_t) * Schedule->Size);
	memset(Schedule->Frames, 0, sizeof(size_t) * Schedule->Size);

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
int UsbSchedulerValidate(UsbScheduler_t *Schedule, size_t Period, size_t Bandwidth, size_t TransferCount)
{
	/* Vars */
	int Locked = 0;
	size_t i, j;

	/* Sanitize period */
	if (Period == 0) Period = 1;
	if (Period > Schedule->Size) Period = Schedule->Size;

	/* Iterate requested period */
	for (i = 0; i < Schedule->Size; )
	{
		/* Sanitize initial bandwidth */
		if ((Schedule->Frames[i] + Bandwidth) > Schedule->MaxMaskBandwidth) 
		{
			/* Try to validate on uneven frames 
			 * if period is not 1 */

			/* Sanity */
			if (Period == 1
				|| Locked == 1)
				return -1;
			else {
				i += (1 * Schedule->MaskSize);
				continue;
			}
		}

		/* Yay */
		Locked = 1;

		/* For EHCI we must support bandwidth sizes 
		 * instead of having a scheduler with multiple
		 * levels, we have it flattened */
		if (TransferCount > 1
			&& Schedule->MaskSize > 1)
		{
			/* Vars 
			 * We know the initial frame is free 
			 * so we don't check it */
			size_t FreeFrames = 1;

			/* Find space in 'sub' schedule */
			for (j = 1; j < Schedule->MaskSize; j++) {
				/* Validate Bandwidth on this mask */
				if ((Schedule->Frames[i + j] + Bandwidth) <= Schedule->MaxBandwidth)
					FreeFrames++;
			}

			/* Sanity */
			if (TransferCount > FreeFrames)
				return -1;
		}

		/* Increase by period */
		i += (Period * Schedule->MaskSize);
	}

	/* Done */
	return 0;
}

/* Reservate Bandwidth 
 * This function actually makes the reservation 
 * Validate Bandwith should have been called first */
int UsbSchedulerReserveBandwidth(UsbScheduler_t *Schedule, size_t Period, size_t Bandwidth, 
	size_t TransferCount, size_t *StartFrame, size_t *FrameMask)
{
	/* Vars */
	size_t sMask = 0, sFrame = 0;
	int RetVal = 0;
	size_t i, j;

	/* Sanitize period */
	if (Period == 0) Period = 1;
	if (Period > Schedule->Size) Period = Schedule->Size;

	/* Request lock */
	SpinlockAcquire(&Schedule->Lock);

	/* Validate (With Lock)! */
	if (UsbSchedulerValidate(Schedule, Period, Bandwidth, TransferCount)) {
		RetVal = -1;
		goto GoOut;
	}

	/* Iterate requested period */
	for (i = 0; i < Schedule->Size; )
	{
		/* Sanitize initial bandwidth */
		if ((Schedule->Frames[i] + Bandwidth) > Schedule->MaxMaskBandwidth)
		{
			/* Try to allocate on uneven frames
			* if period is not 1 */

			/* Sanity */
			if (Period == 1
				|| sFrame != 0)
				return -1;
			else {
				i += (1 * Schedule->MaskSize);
				continue;
			}
		}

		/* Lock this frame-period */
		sFrame = i;

		/* For EHCI we must support bandwidth sizes
		* instead of having a scheduler with multiple
		* levels, we have it flattened */
		if (TransferCount > 1
			&& Schedule->MaskSize > 1)
		{
			/* Either we create a mask
			 * or we allocate a mask */
			if (sMask == 0)
			{
				/* Vars */
				int Counter = TransferCount;

				/* Find space in 'sub' schedule */
				for (j = 1; j < Schedule->MaskSize && Counter; j++) {
					if ((Schedule->Frames[i + j] + Bandwidth) <= Schedule->MaxBandwidth) {
						Schedule->Frames[i + j] += Bandwidth;
						sMask |= (1 << j);
						Counter--;
					}
				}
			}
			else
			{
				/* Allocate bandwidth in 'sub' schedule */
				for (j = 1; j < Schedule->MaskSize; j++) {
					if (sMask & (1 << j)) {
						Schedule->Frames[i + j] += Bandwidth;
					}
				}
			}
		}

		/* Allocate outer */
		Schedule->Frames[i] += Bandwidth;

		/* Increase by period */
		i += (Period * Schedule->MaskSize);
	}

GoOut:
	/* Release lock */
	SpinlockRelease(&Schedule->Lock);

	/* Save parameters */
	if (StartFrame != NULL)
		*StartFrame = sFrame;
	if (FrameMask != NULL)
		*FrameMask = sMask;

	/* Done */
	return RetVal;
}

/* Release Bandwidth */
int UsbSchedulerReleaseBandwidth(UsbScheduler_t *Schedule, size_t Period, 
	size_t Bandwidth, size_t StartFrame, size_t FrameMask)
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
	for (i = StartFrame; i < Schedule->Size; i += (Period * Schedule->MaskSize))
	{
		/* Allocate outer */
		Schedule->Frames[i] -= MIN(Bandwidth, Schedule->Frames[i]);

		/* For EHCI we must support bandwidth sizes
		* instead of having a scheduler with multiple
		* levels, we have it flattened */
		if (FrameMask != 0
			&& Schedule->MaskSize > 1)
		{
			/* Free bandwidth in 'sub' schedule */
			for (j = 1; j < Schedule->MaskSize; j++) {
				if (FrameMask & (1 << j)) {
					Schedule->Frames[i + j] -= Bandwidth;
				}
			}
		}
	}

	/* Release lock */
	SpinlockRelease(&Schedule->Lock);

	/* Done */
	return RetVal;
}