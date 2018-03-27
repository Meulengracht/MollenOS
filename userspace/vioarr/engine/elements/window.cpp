/* MollenOS
 *
 * Copyright 2011 - 2018, Philip Meulengracht
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
 * MollenOS - Vioarr Window Compositor System
 *  - The window compositor system and general window manager for
 *    MollenOS.
 */
#include "window.hpp"

CWindow::CWindow(CEntity *Parent, NVGcontext* VgContext, 
    const std::string &Title, int Width, int Height) : CEntity(Parent, VgContext) {
    m_Title     = Title;
    m_Width     = Width;
    m_Height    = Height;
    m_Active    = true;
}

CWindow::CWindow(NVGcontext* VgContext, const std::string &Title, int Width, int Height) 
    : CWindow(nullptr, VgContext, Title, Width, Height) { }

CWindow::~CWindow() {
}

void CWindow::Update(size_t MilliSeconds) {
}

void CWindow::Draw(NVGcontext* VgContext) {
    // Variables
	NVGpaint ShadowPaint;
    float x = 0.0f, y = 0.0f;

	// Window
	nvgBeginPath(VgContext);
	nvgRoundedRect(VgContext, x, y, m_Width, m_Height, WINDOW_CORNER_RADIUS);
	nvgFillColor(VgContext, nvgRGBA(WINDOW_FILL_COLOR_RGBA));
	nvgFill(VgContext);

	// Drop shadow
	ShadowPaint = nvgBoxGradient(VgContext, x, y + 2.0f, m_Width, m_Height, WINDOW_CORNER_RADIUS * 2, 10, nvgRGBA(0, 0, 0, 128), nvgRGBA(0, 0, 0, 0));
	nvgBeginPath(VgContext);
	nvgRect(VgContext, x - 10, y - 10, m_Width + 20, m_Height + 30);
	nvgRoundedRect(VgContext, x, y, m_Width, m_Height, WINDOW_CORNER_RADIUS);
	nvgPathWinding(VgContext, NVG_HOLE);
	nvgFillPaint(VgContext, ShadowPaint);
	nvgFill(VgContext);

    // Adjust y again to point at the top of the window
    y += (m_Height - WINDOW_HEADER_HEIGHT);

	// Header
	nvgBeginPath(VgContext);
	nvgRoundedRectVarying(VgContext, x, y, m_Width, WINDOW_HEADER_HEIGHT, 0.0f, 0.0f, WINDOW_CORNER_RADIUS - 1, WINDOW_CORNER_RADIUS - 1);
	nvgFillColor(VgContext, m_Active ? nvgRGBA(WINDOW_HEADER_ACTIVE_RGBA) : nvgRGBA(WINDOW_HEADER_INACTIVE_RGBA));
	nvgFill(VgContext);
	
    // Text
    //nvgBeginPath(vg);
	//nvgMoveTo(vg, x+0.5f, y+0.5f+30);
	//nvgLineTo(vg, x+0.5f+w-1, y+0.5f+30);
	//nvgStrokeColor(vg, nvgRGBA(0,0,0,32));
	//nvgStroke(vg);

	//nvgFontSize(vg, 18.0f);
	//nvgFontFace(vg, "sans-bold");
	//nvgTextAlign(vg,NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE);

	//nvgFontBlur(vg,2);
	//nvgFillColor(vg, nvgRGBA(0,0,0,128));
	//nvgText(vg, x+w/2,y+16+1, title, NULL);

	//nvgFontBlur(vg,0);
	//nvgFillColor(vg, nvgRGBA(220,220,220,160));
	//nvgText(vg, x+w/2,y+16, title, NULL);
}
