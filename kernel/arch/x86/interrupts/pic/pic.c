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
* MollenOS X86-32 PIC (Programmable Interrupt Controller)
*
*/

/* Includes */
#include <arch.h>
#include <pic.h>

/* Initializes and disables */
void PicInit(void)
{
	/* Inititalize & Remap PIC. WE HAVE TO DO THIS :( */

	/* Send INIT (0x10) and IC4 (0x1) Commands*/
	outb(0x20, 0x11);
	outb(0xA0, 0x11);

	/* Remap primary PIC to 0x20 - 0x28 */
	outb(0x21, 0x20);

	/* Remap Secondary PIC to 0x28 - 0x30 */
	outb(0xA1, 0x28);

	/* Send initialization words, they define
	 * which PIC connects to where */
	outb(0x21, 0x04);
	outb(0xA1, 0x02);

	/* Enable i86 mode */
	outb(0x21, 0x01);
	outb(0xA1, 0x01);

	/* Mask all irqs in PIC */
	outb(0x21, 0xFF);
	outb(0xA1, 0xFF);
}