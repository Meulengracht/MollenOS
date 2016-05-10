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
* MollenOS Window - Sapphire
*/

#ifndef _SAPPHIRE_WINDOW_H_
#define _SAPPHIRE_WINDOW_H_

/* Includes */
#include <crtdefs.h>
#include <stddef.h>
#include <stdint.h>

/* Ui Includes */
#include <SDL.h>

/* Definitions */


/* Structures */
typedef struct _sWindow {

	/* Window Id */
	int Id;

	/* Window Z-Index */
	int zIndex;
	
	/* The render surface */
	SDL_Texture *Texture;

} Window_t;


/* Prototypes */



/* Destructor
 * Cleans up and releases 
 * resources allocated */
EXTERN void WindowDestroy(Window_t *Window);

#endif //!_SAPPHIRE_WINDOW_H_