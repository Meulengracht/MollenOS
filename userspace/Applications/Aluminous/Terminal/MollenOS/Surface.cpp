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

/* Includes */
#include "Surface.h"
#include <cstddef>

/* MollenOS Guard
* So other subsystems don't include this */
#ifdef MOLLENOS

/* The surface constructor 
 * Initializes a new mollenos window 
 * and gets the backbuffer */
Surface::Surface()
{
	/* Set some default params */
	m_bIsValid = true;

	/* Create default size of terminal */
	m_sDimensions.x = 100;
	m_sDimensions.y = 100;
	m_sDimensions.w = 600;
	m_sDimensions.h = 400;

	/* Create the window */
	m_pHandle = UiCreateWindow(&m_sDimensions, 0);

	/* Query the backbuffer information */
	UiQueryBackbuffer(m_pHandle, &m_pBuffer, &m_iBufferSize);
}

/* Cleans up the surface, and destroys the window
 * allocated for the surface */
Surface::~Surface()
{
	/* Destroy the window */
	UiDestroyWindow(m_pHandle);
}

/* Clear out surface with the background color
 * Use this for cleaning, for a full clear use
 * NULL in Area */
void Surface::Clear(uint32_t Color, Rect_t *Area)
{
	/* Calculate the number of iterations 
	 * in bytes of 4 */
	uint32_t *ItrPtr = (uint32_t*)m_pBuffer;
	size_t Calculations = m_iBufferSize / 4;

	/* Iterate and set color */
	for (size_t i = 0; i < Calculations; i++, ItrPtr++) {
		*ItrPtr = Color;
	}

	/* Invalidate */
	UiInvalidateRect(m_pHandle, NULL);
}

/* Invalidate surface with the
 * given rectangle dimensions, by logical units */
void Surface::Invalidate(int x, int y, int width, int height)
{
	/* Sanitize params 
	 * full invalidate? */
	Rect_t Dirty;

	if (width == -1 && height == -1) {
		UiInvalidateRect(m_pHandle, NULL);
	}
	else {
		Dirty.x = x;
		Dirty.y = y;

		if (width == -1) {
			Dirty.w = m_sDimensions.w;
		}
		else {
			Dirty.w = width;
		}

		if (height == -1) {
			Dirty.h = m_sDimensions.h;
		}
		else {
			Dirty.h = height;
		}

		/* Call the invalidation */
		UiInvalidateRect(m_pHandle, &Dirty);
	}
}

/* Resize the canvas, so we can support that! */
void Surface::Resize(int Width, int Height)
{

}

/* Is surface valid for rendering? In many
* cases it won't be before size has been set */
bool Surface::IsValid() {
	return m_bIsValid;
}

/* Retrieves a surface data pointer for accessing
* raw pixels on our surface - direct drawing */
void *Surface::DataPtr(int OffsetX, int OffsetY)
{
	/* Cast to a modifiable pointer */
	uint8_t *Ptr = (uint8_t*)m_pBuffer;

	/* Increase by offsets */
	Ptr += ((OffsetY * (m_sDimensions.w * 4)) + OffsetX * 4);

	/* Done! */
	return (void*)Ptr;
}

/* Helper, it combines different color components
* into a full color */
uint32_t Surface::GetColor(uint8_t R, uint8_t G, uint8_t B, uint8_t A)
{
	/* Combine them into ARGB for now.. 
	 * untill we can query pixel format */
	return ((A << 24) | (R << 16) | (G << 8) | B);
}

#endif
