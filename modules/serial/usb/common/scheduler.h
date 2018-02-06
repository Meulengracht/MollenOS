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

#ifndef __USB_SCHEDULER__
#define __USB_SCHEDULER__

/* Includes
 * - Library */
#include <os/contracts/usbhost.h>
#include <os/spinlock.h>
#include <os/osdefs.h>
#include <os/usb.h>

/* Definitions
 * Generic bandwidth allocation constants/support */
#define FRAME_TIME_USECS				1000L
#define FRAME_TIME_BITS                 12000L
#define FRAME_TIME_MAX_BITS_ALLOC       (90L * FRAME_TIME_BITS / 100L)
#define FRAME_TIME_MAX_USECS_ALLOC      (90L * FRAME_TIME_USECS / 100L)

#define BitTime(bytecount)				(7 * 8 * bytecount / 6)
#define NS_TO_US(ns)					DIVUP(ns, 1000L)

/* Full/low speed bandwidth allocation constants/support. */
#define BW_HOST_DELAY   1000L
#define BW_HUB_LS_SETUP 333L

/* Ceiling [nano/micro]seconds (typical) for that many bytes at high speed
 * ISO is a bit less, no ACK ... from USB 2.0 spec, 5.11.3 (and needed
 * to preallocate bandwidth) */
#define USB2_HOST_DELAY 5       // nsec, guess
#define HS_NSECS(bytes) (((55 * 8 * 2083) \
        + (2083UL * (3 + BitTime(bytes))))/1000 \
        + USB2_HOST_DELAY)
#define HS_NSECS_ISO(bytes) (((38 * 8 * 2083) \
        + (2083UL * (3 + BitTime(bytes))))/1000 \
        + USB2_HOST_DELAY)
#define HS_USECS(bytes)         NS_TO_US(HS_NSECS(bytes))
#define HS_USECS_ISO(bytes)     NS_TO_US(HS_NSECS_ISO(bytes))

/* UsbScheduler
 * Contains information neccessary to keep track of scheduling
 * bandwidths and which frames are occupied. As generic as possible
 * to be usuable by all controllers */
typedef struct _UsbScheduler {
	size_t					 Size;
	size_t 					 MaskSize;
	size_t 					 MaxBandwidth;
	size_t 					 MaxMaskBandwidth;
	size_t 					 TotalBandwidth;
	size_t 					*Frames;
	Spinlock_t 				 Lock;
} UsbScheduler_t;

/* UsbSchedulerInitialize 
 * Initializes a new instance of a scheduler that can be used to
 * keep track of controller bandwidth and which frames are active.
 * MaxBandwidth is usually either 800 or 900. */
__EXTERN
UsbScheduler_t*
UsbSchedulerInitialize(
	_In_ size_t Size,
	_In_ size_t MaxBandwidth,
	_In_ size_t MaskSize);

/* UsbSchedulerDestroy 
 * Cleans up any resources allocated by the scheduler */
__EXTERN
OsStatus_t
UsbSchedulerDestroy(
	_In_ UsbScheduler_t *Schedule);

/* UsbCalculateBandwidth
 * This function calculates the approx time a transfer 
 * needs to spend on the bus in NS. */
__EXTERN
long
UsbCalculateBandwidth(
	_In_ UsbSpeed_t Speed, 
	_In_ int Direction,
	_In_ UsbTransferType_t Type,
	_In_ size_t Length);

/* UsbSchedulerValidate
 * This function makes sure there is enough 
 * room for the requested bandwidth 
 * Period => Is the actual frequency we want it occuring in ms
 * Bandwidth => Is the NS required for allocation */
__EXTERN
OsStatus_t
UsbSchedulerValidate(
	_In_ UsbScheduler_t *Schedule,
	_In_ size_t Period,
	_In_ size_t Bandwidth,
	_In_ size_t TransferCount);

/* UsbSchedulerReserveBandwidth
 * This function actually makes the reservation 
 * Validate Bandwith should have been called first */
__EXTERN
OsStatus_t
UsbSchedulerReserveBandwidth(
	_In_ UsbScheduler_t *Schedule, 
	_In_ size_t Period, 
	_In_ size_t Bandwidth, 
	_In_ size_t TransferCount,
	_Out_ size_t *StartFrame,
	_Out_ size_t *FrameMask);

/* UsbSchedulerReleaseBandwidth 
 * Release the given amount of bandwidth, the StartFrame and FrameMask must
 * be obtained from the ReserveBandwidth function */
__EXTERN
OsStatus_t
UsbSchedulerReleaseBandwidth(
	_In_ UsbScheduler_t *Schedule, 
	_In_ size_t Period, 
	_In_ size_t Bandwidth, 
	_In_ size_t StartFrame, 
	_In_ size_t FrameMask);

#endif //!__USB_SCHEDULER__
