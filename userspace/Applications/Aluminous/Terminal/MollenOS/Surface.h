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
	 * Use this for cleaning, for a full clear use
	 * NULL in Area */
	void Clear(uint32_t Color, Rect_t *Area);

	/* Resize the canvas, so we can support that! */
	void Resize(int Width, int Height);

	/* Invalidate surface with the 
	 * given rectangle dimensions, by logical units */
	void Invalidate(int x, int y, int width, int height);

	/* Is surface valid for rendering? In many 
	 * cases it won't be before size has been set */
	bool IsValid();

	/* Retrieves a surface data pointer for accessing
	 * raw pixels on our surface - direct drawing */
	void *DataPtr(int OffsetX = 0, int OffsetY = 0);

	/* Get dimensions of this surface 
	 * Can be useful for boundary checks */
	Rect_t *GetDimensions() { return &m_sDimensions; }

	/* Helper, it combines different color components
	 * into a full color */
	uint32_t GetColor(uint8_t R, uint8_t G, uint8_t B, uint8_t A);

private:
	/* Private - Functions */

	/* Private - Data */
	WndHandle_t m_pHandle;
	void *m_pBuffer;
	size_t m_iBufferSize;

	/* Private - State */
	Rect_t m_sDimensions;
	bool m_bIsValid;
};

#endif //!MOLLENOS

#endif //!_MOLLENOS_SURFACE_H_
