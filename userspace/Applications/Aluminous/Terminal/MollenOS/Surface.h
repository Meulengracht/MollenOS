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
* MollenOS Terminal Implementation
* - Project Alumnious (First C++ Project)
*/

#ifndef _MOLLENOS_SURFACE_H_
#define _MOLLENOS_SURFACE_H_

/* MollenOS Guard 
 * So other subsystems don't include this */
#ifdef MOLLENOS

/* Includes */
#include <os/MollenOS.h>
#include <os/Ui.h>

/* Class */
class Surface
{
public:
	Surface();
	~Surface();

	/* Clear out surface with the background color
	 * Use this for cleaning */
	void Clear();

	/* Resize the canvas, so we can support that! */
	void Resize(int Width, int Height);

	/* Is surface valid for rendering? In many 
	 * cases it won't be before size has been set */
	bool IsValid();

	/* Retrieves a surface data pointer for accessing
	 * raw pixels on our surface - direct drawing */
	void *DataPtr(size_t Offset = 0);

private:
	/* Private - Functions */
};

#endif //!MOLLENOS

#endif //!_MOLLENOS_SURFACE_H_
