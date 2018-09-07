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
* - Project Alumnious
*/

#include "surface_vali.hpp"
#include <cstddef>

void BufferFill(const char *Buffer, uint32_t Color, size_t Size) {
    uint32_t *Pointer       = (uint32_t*)Buffer;
    size_t NumOfIterations  = Size / 4;
    while (NumOfIterations) {
        *(Pointer++) = Color;
        NumOfIterations--;
    }
}

/*******************************************
 * Windowing Tests
 *******************************************/
void BasicWindowingTests() {
    UIWindowParameters_t WindowParameters   = { { { 0 } } };
    DmaBuffer_t *WindowBuffer               = NULL;
    UiParametersSetDefault(&WindowParameters);

    // Register the window as initial step
    UiRegisterWindow(&WindowParameters, &WindowBuffer);
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // Perform a window fill of color R
    BufferFill((const char*)GetBufferDataPointer(WindowBuffer), 
        0xFFFF0000, GetBufferSize(WindowBuffer));
    UiSwapBackbuffer();
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // Perform a window fill of color G
    BufferFill((const char*)GetBufferDataPointer(WindowBuffer), 
        0xFF00FF00, GetBufferSize(WindowBuffer));
    UiSwapBackbuffer();
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // Perform a window fill of color B
    BufferFill((const char*)GetBufferDataPointer(WindowBuffer), 
        0xFF0000FF, GetBufferSize(WindowBuffer));
    UiSwapBackbuffer();
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // Destroy window and cleanup
    UiUnregisterWindow();
}


CValiSurface::CValiSurface(CSurfaceRect& Dimensions)
    : CSurface(Dimensions)
{
    UiParametersSetDefault(&m_WindowParameters);

    // Manually update these from passed dimensions
    m_WindowParameters.Surface.Dimensions.w = Dimensions.GetWidth();
    m_WindowParameters.Surface.Dimensions.h = Dimensions.GetHeight();
    UiRegisterWindow(&m_WindowParameters, &m_WindowBuffer);
}

CValiSurface::~CValiSurface() {
    UiUnregisterWindow();
}

void CValiSurface::Clear(uint32_t Color, CSurfaceRect& Area)
{

    // Invalidate
    Invalidate();
}
void CValiSurface::Resize(int Width, int Height) {

}

void CValiSurface::Invalidate() {
    UiSwapBackbuffer();
}


uint8_t* CValiSurface::GetDataPointer(int OffsetX = 0, int OffsetY = 0) {
    uint8_t *Pointer = (uint8_t*)GetBufferDataPointer(m_WindowBuffer);
    Pointer += ((OffsetY * m_iBufferPitch) + (OffsetX * 4));
    return Pointer;
}

// Color helpers
uint32_t GetBlendedColor(uint8_t RA, uint8_t GA, uint8_t BA, uint8_t AA,
    uint8_t RB, uint8_t GB, uint8_t BB, uint8_t AB, uint8_t A) override;
uint32_t GetColor(uint8_t R, uint8_t G, uint8_t B, uint8_t A) override;

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

/* Helper, it combines different color components
 * into a full color, using color blend */
uint32_t Surface::GetBlendedColor(uint8_t RA, uint8_t GA, uint8_t BA, uint8_t AA,
	uint8_t RB, uint8_t GB, uint8_t BB, uint8_t AB, uint8_t A)
{
	uint32_t ColorA = GetColor(RA, GA, BA, AA);
	uint32_t ColorB = GetColor(RB, GB, BB, AB);
	uint32_t RB1 = ((0x100 - A) * (ColorA & 0xFF00FF)) >> 8;
	uint32_t RB2 = (A * (ColorB & 0xFF00FF)) >> 8;
	uint32_t G1 = ((0x100 - A) * (ColorA & 0x00FF00)) >> 8;
	uint32_t G2 = (A * (ColorB & 0x00FF00)) >> 8;
	return (((RB1 | RB2) & 0xFF00FF) + ((G1 | G2) & 0x00FF00)) | 0xFF000000;
}

/* Helper, it combines different color components
 * into a full color */
uint32_t Surface::GetColor(uint8_t R, uint8_t G, uint8_t B, uint8_t A)
{
	/* Combine them into ARGB for now.. 
	 * untill we can query pixel format */
	return ((A << 24) | (R << 16) | (G << 8) | B);
}
