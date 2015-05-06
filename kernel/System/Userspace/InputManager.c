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
ringbuffer_t *pointer_pipe = NULL;
ringbuffer_t *button_pipe = NULL;
volatile uint32_t glb_im_initialized = 0;

/* Initialise Input Manager */
void input_manager_init(void)
{
	/* Allocate Pipes */
	pointer_pipe = ringbuffer_create(0x1000);
	button_pipe = ringbuffer_create(0x1000);

	/* Set initialized */
	glb_im_initialized = 0xDEADBEEF;
}

/* Write data to pointer pipe */
void input_manager_send_pointer_data(input_pointer_data_t *data)
{
	input_pointer_data_t not_recycle_bin;

	/* Sanity */
	if (glb_im_initialized != 0xDEADBEEF)
		input_manager_init();

	/* Force space in buffer */
	while (ringbuffer_size(pointer_pipe) < sizeof(input_pointer_data_t))
		ringbuffer_read(pointer_pipe, sizeof(input_pointer_data_t), (uint8_t*)&not_recycle_bin);

	/* Write data to pipe */
	ringbuffer_write(pointer_pipe, sizeof(input_pointer_data_t), (uint8_t*)data);
}

/* Write data to button pipe */
void input_manager_send_button_data(input_button_data_t *data)
{
	input_button_data_t not_recycle_bin;

	/* Sanity */
	if (glb_im_initialized != 0xDEADBEEF)
		input_manager_init();

	/* Force space in buffer */
	while (ringbuffer_size(button_pipe) < sizeof(input_button_data_t))
		ringbuffer_read(button_pipe, sizeof(input_button_data_t), (uint8_t*)&not_recycle_bin);

	/* Write data to pipe */
	ringbuffer_write(button_pipe, sizeof(input_button_data_t), (uint8_t*)data);
}