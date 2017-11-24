/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * MollenOS MCore - USB Controller Scheduler
 * - Contains the implementation of a shared controller scheduker
 *   for all the usb drivers
 */
//#define __TRACE

/* Includes
 * - System */
#include <os/mollenos.h>
#include <os/utils.h>
#include "scheduler.h"

/* Includes
 * - Library */
#include <stddef.h>
#include <stdlib.h>

/* UsbSchedulerInitialize 
 * Initializes a new instance of a scheduler that can be used to
 * keep track of controller bandwidth and which frames are active.
 * MaxBandwidth is usually either 800 or 900. */
UsbScheduler_t*
UsbSchedulerInitialize(
	_In_ size_t Size,
	_In_ size_t MaxBandwidth,
	_In_ size_t MaskSize)
{
	// Variables
	UsbScheduler_t *Schedule = NULL;

	// Allocate a new instance and zero it out
	Schedule = (UsbScheduler_t*)malloc(sizeof(UsbScheduler_t));
	memset(Schedule, 0, sizeof(UsbScheduler_t));

	// Sanity, if mask size is not 1 we must allow for even more 
	// to allow both an 'overview' and their submembers
	if (MaskSize != 1) {
		MaskSize++;
	}

	// Set initial members of the scheduler
	Schedule->Size = Size * MaskSize;
	Schedule->MaskSize = MaskSize;
	Schedule->MaxBandwidth = MaxBandwidth;
	SpinlockReset(&Schedule->Lock);

	// Adjust max-mask bandwidth based on masksize if set
	if (MaskSize != 1) {
		Schedule->MaxMaskBandwidth = (MaxBandwidth * (MaskSize - 1));
	}
	else {
		Schedule->MaxMaskBandwidth = MaxBandwidth;
	}
	
	// Allocate frame list 
	// and make sure it's zero'd out
	Schedule->Frames = (size_t*)malloc(sizeof(size_t) * Schedule->Size);
	memset(Schedule->Frames, 0, sizeof(size_t) * Schedule->Size);

	// Setup done
	return Schedule;
}

/* UsbSchedulerDestroy 
 * Cleans up any resources allocated by the scheduler */
OsStatus_t
UsbSchedulerDestroy(
	_In_ UsbScheduler_t *Schedule)
{
	// Free the frame-list and the base structure
	free(Schedule->Frames);
	free(Schedule);
	return OsSuccess;
}

/* UsbCalculateBandwidth
 * This function calculates the approx time a transfer 
 * needs to spend on the bus in NS. */
long
UsbCalculateBandwidth(
	_In_ UsbSpeed_t Speed, 
	_In_ int Direction,
	_In_ UsbTransferType_t Type,
	_In_ size_t Length)
{
	// Variables
	long Result = 0;

	// The bandwidth calculations are based entirely
	// on the speed of the transfer
	switch (Speed) {
	case LowSpeed:
		if (Direction == USB_ENDPOINT_IN) {
			Result = (67667L * (31L + 10L * BitTime(Length))) / 1000L;
			return 64060L + (2 * BW_HUB_LS_SETUP) + BW_HOST_DELAY + Result;
		}
		else {
			Result = (66700L * (31L + 10L * BitTime(Length))) / 1000L;
			return 64107L + (2 * BW_HUB_LS_SETUP) + BW_HOST_DELAY + Result;
		}
	case FullSpeed:
		if (Type == IsochronousTransfer) {
			Result = (8354L * (31L + 10L * BitTime(Length))) / 1000L;
			return ((Direction == USB_ENDPOINT_IN) ? 7268L : 6265L) + BW_HOST_DELAY + Result;
		}
		else {
			Result = (8354L * (31L + 10L * BitTime(Length))) / 1000L;
			return 9107L + BW_HOST_DELAY + Result;
		}
	case SuperSpeed:
	case HighSpeed:
		if (Type == IsochronousTransfer)
			Result = HS_NSECS_ISO(Length);
		else
			Result = HS_NSECS(Length);
	}

	// Return the computed value
	return Result;
}

/* UsbSchedulerValidate
 * This function makes sure there is enough 
 * room for the requested bandwidth 
 * Period => Is the actual frequency we want it occuring in ms
 * Bandwidth => Is the NS required for allocation */
OsStatus_t
UsbSchedulerValidate(
	_In_ UsbScheduler_t *Schedule,
	_In_ size_t Period,
	_In_ size_t Bandwidth,
	_In_ size_t TransferCount)
{
	// Variables
	int Locked = 0;
	size_t i, j;

	// Sanitize some bounds for period
	// to be considered if it should fail instead
	if (Period == 0) Period = 1;
	if (Period > Schedule->Size) Period = Schedule->Size;

	// Iterate the requested period and make sure
	// there is actually room
	for (i = 0; i < Schedule->Size; ) {
		if ((Schedule->Frames[i] + Bandwidth) > Schedule->MaxMaskBandwidth) {
			// Try to validate on uneven frames 
			// if period is not 1
			if (Period == 1 || Locked == 1) {
				return OsError;
			}
			else {
				i += (1 * Schedule->MaskSize);
				continue;
			}
		}

		// We are now locked
		Locked = 1;

		// For EHCI we must support bandwidth sizes 
		// instead of having a scheduler with multiple
		// levels, we have it flattened
		if (TransferCount > 1 && Schedule->MaskSize > 1) {
			// We know the initial frame is free 
			// so we don't check it
			size_t FreeFrames = 1;

			// Find space in 'sub' schedule
			for (j = 1; j < Schedule->MaskSize; j++) {
				if ((Schedule->Frames[i + j] + Bandwidth) 
						<= Schedule->MaxBandwidth) {
					FreeFrames++;
				}
			}

			// Do we have enough free frames?
			if (TransferCount > FreeFrames) {
				return OsError;
			}
		}

		// Increase by period to get next index
		i += (Period * Schedule->MaskSize);
	}

	// Success
	return OsSuccess;
}

/* UsbSchedulerReserveBandwidth
 * This function actually makes the reservation 
 * Validate Bandwith should have been called first */
OsStatus_t
UsbSchedulerReserveBandwidth(
	_In_ UsbScheduler_t *Schedule, 
	_In_ size_t Period, 
	_In_ size_t Bandwidth, 
	_In_ size_t TransferCount,
	_Out_ size_t *StartFrame,
	_Out_ size_t *FrameMask)
{
	// Variables
	OsStatus_t Result = OsSuccess;
	size_t sMask = 0, sFrame = 0;
	size_t i, j;

	// Sanitize some bounds for period
	// to be considered if it should fail instead
	if (Period == 0) Period = 1;
	if (Period > Schedule->Size) Period = Schedule->Size;

	// Acquire the lock, as we can't be interfered with
	SpinlockAcquire(&Schedule->Lock);

	// Validate the bandwidth again
	if (UsbSchedulerValidate(Schedule, Period, Bandwidth, TransferCount)) {
		Result = OsError;
		goto GoOut;
	}

	// Iterate the requested period and make sure
	// there is actually room
	for (i = 0; i < Schedule->Size; ) {
		if ((Schedule->Frames[i] + Bandwidth) > Schedule->MaxMaskBandwidth) {
			// Try to allocate on uneven frames
			// if period is not 1
			if (Period == 1 || sFrame != 0) {
				Result = OsError;
				goto GoOut;
			}
			else {
				i += (1 * Schedule->MaskSize);
				continue;
			}
		}

		// Lock this frame-period
		sFrame = i;

		// For EHCI we must support bandwidth sizes
		// instead of having a scheduler with multiple
		// levels, we have it flattened
		if (TransferCount > 1 && Schedule->MaskSize > 1) {
			// Either we create a mask
			// or we allocate a mask
			if (sMask == 0) {
				int Counter = TransferCount;
				// Find space in 'sub' schedule
				for (j = 1; j < Schedule->MaskSize && Counter; j++) {
					if ((Schedule->Frames[i + j] + Bandwidth) <= Schedule->MaxBandwidth) {
						Schedule->Frames[i + j] += Bandwidth;
						sMask |= (1 << j);
						Counter--;
					}
				}
			}
			else {
				// Allocate bandwidth in 'sub' schedule
				for (j = 1; j < Schedule->MaskSize; j++) {
					if (sMask & (1 << j)) {
						Schedule->Frames[i + j] += Bandwidth;
					}
				}
			}
		}

		// Increase bandwidth and update index by period
		Schedule->Frames[i] += Bandwidth;
		i += (Period * Schedule->MaskSize);
	}

GoOut:
	// Release the lock and save parameters
	SpinlockRelease(&Schedule->Lock);
	if (StartFrame != NULL) {
		*StartFrame = sFrame;
	}
	if (FrameMask != NULL) {
		*FrameMask = sMask;
	}
	
	// Done
	return Result;
}

/* UsbSchedulerReleaseBandwidth 
 * Release the given amount of bandwidth, the StartFrame and FrameMask must
 * be obtained from the ReserveBandwidth function */
OsStatus_t
UsbSchedulerReleaseBandwidth(
	_In_ UsbScheduler_t *Schedule, 
	_In_ size_t Period, 
	_In_ size_t Bandwidth, 
	_In_ size_t StartFrame, 
	_In_ size_t FrameMask)
{
	// Variables
	OsStatus_t Result = OsSuccess;
	size_t i, j;

	// Sanitize some bounds for period
	// to be considered if it should fail instead
	if (Period == 0) Period = 1;
	if (Period > Schedule->Size) Period = Schedule->Size;

	// Acquire the lock, as we can't be interfered with
	SpinlockAcquire(&Schedule->Lock);

	// Iterate the requested period and make sure
	// there is actually room
	for (i = StartFrame; i < Schedule->Size; i += (Period * Schedule->MaskSize)) {
		// Reduce allocated bandwidth
		Schedule->Frames[i] -= MIN(Bandwidth, Schedule->Frames[i]);

		// For EHCI we must support bandwidth sizes
		// instead of having a scheduler with multiple
		// levels, we have it flattened
		if (FrameMask != 0 && Schedule->MaskSize > 1) {
			// Free bandwidth in 'sub' schedule
			for (j = 1; j < Schedule->MaskSize; j++) {
				if (FrameMask & (1 << j)) {
					Schedule->Frames[i + j] -= Bandwidth;
				}
			}
		}
	}

	// Release the lock and return
	SpinlockRelease(&Schedule->Lock);
	return Result;
}
