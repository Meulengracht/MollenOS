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
* MollenOS Input Manager
*/

/* Includes */
#include <Arch.h>
#include <InputManager.h>
#include <RingBuffer.h>
#include <stddef.h>

/* Globals */
RingBuffer_t *PointerEventPipe = NULL;
RingBuffer_t *ButtonEventPipe = NULL;
volatile uint32_t GlbImInitialized = 0;

/* Initialise Input Manager */
void ImInit(void)
{
	/* Allocate Pipes */
	PointerEventPipe = RingBufferCreate(0x1000);
	ButtonEventPipe = RingBufferCreate(0x1000);

	/* Set initialized */
	GlbImInitialized = 1;
}

/* Write data to pointer pipe */
void InputManagerCreatePointerEvent(ImPointerEvent_t *Event)
{
	ImPointerEvent_t NotRecycleBin;

	/* Sanity */
	if (GlbImInitialized != 1)
		ImInit();

	/* Force space in buffer */
	while (RingBufferSpaceAvailable(PointerEventPipe) < sizeof(ImPointerEvent_t))
		RingBufferRead(PointerEventPipe, sizeof(ImPointerEvent_t), (uint8_t*)&NotRecycleBin);

	/* Write data to pipe */
	RingBufferWrite(PointerEventPipe, sizeof(ImPointerEvent_t), (uint8_t*)Event);
}

/* Write data to button pipe */
void InputManagerCreateButtonEvent(ImButtonEvent_t *Event)
{
	ImButtonEvent_t NotRecycleBin;

	/* Sanity */
	if (GlbImInitialized != 1)
		ImInit();

	/* Force space in buffer */
	while (RingBufferSpaceAvailable(ButtonEventPipe) < sizeof(ImButtonEvent_t))
		RingBufferRead(ButtonEventPipe, sizeof(ImButtonEvent_t), (uint8_t*)&NotRecycleBin);

	/* Write data to pipe */
	RingBufferWrite(ButtonEventPipe, sizeof(ImButtonEvent_t), (uint8_t*)Event);
}