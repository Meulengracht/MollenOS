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
* MollenOS MCore - Input Device Descriptor
*/
#ifndef _MCORE_DEVICE_INPUT_H_
#define _MCORE_DEVICE_INPUT_H_

/* Includes */
#include <InputManager.h>
#include <stdint.h>

/* Storage Device */
#pragma pack(push, 1)
typedef struct _MCoreInputDevice
{
	/* Input Data */
	void *InputData;

	/* Functions */
	int (*Read)(void *Data);

	/* Reporting */
	void(*ReportPointerEvent)(ImPointerEvent_t *Event);
	void(*ReportButtonEvent)(ImButtonEvent_t *Event);

} MCoreInputDevice_t;
#pragma pack(pop)

#endif //!_MCORE_DEVICE_INPUT_H_