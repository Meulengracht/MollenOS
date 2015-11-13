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
* MollenOS MCore - Timer Device Descriptor
*/
#ifndef _MCORE_DEVICE_TIMER_H_
#define _MCORE_DEVICE_TIMER_H_

/* Includes */
#include <stdint.h>

/* Storage Device */
#pragma pack(push, 1)
typedef struct _MCoreTimerDevice
{
	/* Timer Data */
	void *TimerData;

	/* Functions */
	void (*Stall)(void *Data, uint32_t MilliSeconds);
	void (*Sleep)(void *Data, uint32_t MilliSeconds);
	uint64_t (*GetTicks)(void *Data);

	/* Report, can be used by the driver */
	void (*ReportMs)(uint32_t Ms);

} MCoreTimerDevice_t;
#pragma pack(pop)

#endif //!_MCORE_DEVICE_TIMER_H_