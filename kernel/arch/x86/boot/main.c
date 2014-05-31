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
 * MollenOS x86-32 Init Code.
 */

/* Includes */
#include <arch.h>
#include <stddef.h>
#include <stdio.h>

/* Extern, this function is declared in the MCore project
 * and all platform libs should enter this function */
extern void mcore_entry(void*);

void init(multiboot_info_t *bootinfo, uint32_t kernel_size)
{
	/* Setup output device */
	video_init(bootinfo);
	kernel_size = kernel_size;

	/* Print MollenOS Header */
	printf("MollenOS Operating System - Platform: %s - Layer Version: %s\n", 
		ARCHITECTURE_NAME, ARCHITECTURE_VERSION);
	printf("Written by Philip Meulengracht, Copyright 2011-2014, All Rights Reserved.\n");
	printf("Build Date 31-05-2014\n");

	/* Done with setup! 
	 * This should be called on a new thread */
	mcore_entry(NULL);
}