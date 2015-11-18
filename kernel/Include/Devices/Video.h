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
* MollenOS Video Device
*/

#ifndef _MCORE_DEVICE_VIDEO_H_
#define _MCORE_DEVICE_VIDEO_H_

/* Video Includes */
#include <Arch.h>
#include <MollenOS.h>

/* Video Structures */
typedef enum _MCoreVideoType
{
	VideoTypeText,
	VideoTypeVGA,
	VideoTypeLFB,
	VideoTypeNative

} MCoreVideoType_t;

typedef struct _MCoreVideoDevice
{
	/* Type */
	MCoreVideoType_t Type;

	/* Cursor Position */
	uint32_t CursorX;
	uint32_t CursorY;

	/* Cursor Limits */
	uint32_t CursorStartX;
	uint32_t CursorStartY;
	uint32_t CursorLimitX;
	uint32_t CursorLimitY;

	/* Colors */
	uint32_t FgColor;
	uint32_t BgColor;

	/* Spinlock */
	Spinlock_t Lock;

	/* Architecture Data */
	void *Data;

	/* Functions */
	void (*DrawPixel)(void*, uint32_t, uint32_t, uint32_t);
	void (*DrawCharacter)(void*, int, uint32_t, uint32_t, uint32_t, uint32_t);
	void (*Put)(void*, int);

} MCoreVideoDevice_t;

/* Video Prototypes */
_CRT_EXTERN OsResult_t VideoInit(MCoreVideoDevice_t *OutData, void *BootInfo);

#endif // !_x86_CPU_H_
