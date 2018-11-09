/* MollenOS
 *
 * Copyright 2018, Philip Meulengracht
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

#include <os/ipc/ipc.h>
#include "window.hpp"
#include "label.hpp"

CWindow::CWindow(CEntity* Parent, NVGcontext* VgContext, const std::string &Title, int Width, int Height) 
    : CEntity(Parent, VgContext)
{
    m_Title             = Title;
    m_Width             = Width;
    m_Height            = Height;
    m_Streaming         = false;
    m_Format            = 0;
    m_InternalFormat    = 0;
    m_StreamWidth       = 0;
    m_StreamHeight      = 0;
    m_StreamBuffer      = nullptr;
    m_ResourceId        = 0;

    m_TitleLabel = new CLabel(this, VgContext);
    m_TitleLabel->SetText(Title);
    m_TitleLabel->SetTextAlignment(CLabel::AlignCenter, CLabel::AlignMiddle);
    m_TitleLabel->SetFont("sans-normal");
    m_TitleLabel->SetFontSize(14.0f);
    m_TitleLabel->SetFontColor(nvgRGBA(255, 255, 255, 255));
    m_TitleLabel->SetPosition(m_Width / 2.0f, m_Height - (WINDOW_HEADER_HEIGHT / 2.0f), 0.0f);
}

CWindow::CWindow(NVGcontext* VgContext, const std::string &Title, int Width, int Height) 
    : CWindow(nullptr, VgContext, Title, Width, Height) { }

CWindow::CWindow(CEntity* Parent, NVGcontext* VgContext)
    : CWindow(Parent, VgContext, "Untitled Window", 450, 300) { }

CWindow::CWindow(NVGcontext* VgContext) 
    : CWindow(nullptr, VgContext) { }

CWindow::~CWindow() {
    if (m_ResourceId != 0) {
        nvgDeleteImage(m_VgContext, m_ResourceId);
    }
    if (m_StreamBuffer != nullptr) {
        DestroyBuffer(m_StreamBuffer);
    }
}

void CWindow::SetWidth(int Width) {
    m_Width = Width;
    m_TitleLabel->SetPosition(m_Width / 2.0f, m_Height - (WINDOW_HEADER_HEIGHT / 2.0f), 0.0f);
}

void CWindow::SetHeight(int Height) {
    m_Height = Height + (int)WINDOW_HEADER_HEIGHT;
    m_TitleLabel->SetPosition(m_Width / 2.0f, m_Height - (WINDOW_HEADER_HEIGHT / 2.0f), 0.0f);
}

void CWindow::SetTitle(const std::string &Title) {
    m_Title = Title;
}

void CWindow::SetStreamingBufferFormat(GLenum Format, GLenum InternalFormat) {
    m_Format            = m_Format;
    m_InternalFormat    = m_InternalFormat;
}

void CWindow::SetStreamingBufferDimensions(int Width, int Height) {
    m_StreamWidth   = Width;
    m_StreamHeight  = Height;
}

void CWindow::SetStreamingBuffer(DmaBuffer_t* Buffer) {
    m_StreamBuffer = Buffer;
}

void CWindow::SetStreaming(bool Enable) {
    m_Streaming = Enable;

    if (Enable) {
        m_ResourceId = nvgCreateImageRGBA(m_VgContext, m_StreamWidth, m_StreamHeight, 
            NVG_IMAGE_FLIPY, (const uint8_t*)GetBufferDataPointer(m_StreamBuffer));
        if (m_ResourceId == 0) {
            m_Streaming = false;
        }
    }
    else {
        if (m_ResourceId != 0) {
            nvgDeleteImage(m_VgContext, m_ResourceId);
            m_ResourceId = 0;
        }
    }
}

void CWindow::HandleKeyEvent(SystemKey_t* Key) {
    SendPipe(m_Owner, PIPE_STDIN, (void*)Key, sizeof(SystemKey_t));
}

void CWindow::Update() {
    if (m_Streaming) {
        nvgUpdateImage(m_VgContext, m_ResourceId, (const uint8_t*)GetBufferDataPointer(m_StreamBuffer));
    }
}

void CWindow::Draw(NVGcontext* VgContext) {
	NVGpaint ShadowPaint;
    NVGpaint StreamPaint;
    float x = 0.0f, y = 0.0f;

	// Drop shadow
	ShadowPaint = nvgBoxGradient(VgContext, x, y + 2.0f, m_Width, m_Height, WINDOW_CORNER_RADIUS * 2, 10, nvgRGBA(0, 0, 0, 128), nvgRGBA(0, 0, 0, 0));
	nvgBeginPath(VgContext);
	nvgRect(VgContext, x - 10, y - 10, m_Width + 20, m_Height + 30);
	nvgRoundedRect(VgContext, x, y, m_Width, m_Height, WINDOW_CORNER_RADIUS);
	nvgPathWinding(VgContext, NVG_HOLE);
	nvgFillPaint(VgContext, ShadowPaint);
	nvgFill(VgContext);

	// Window
	nvgBeginPath(VgContext);
	nvgRoundedRect(VgContext, x, y, m_Width, m_Height, WINDOW_CORNER_RADIUS);
	nvgFillColor(VgContext, nvgRGBA(WINDOW_FILL_COLOR_RGBA));
	nvgFill(VgContext);

    // Stream
    if (m_Streaming) {
        StreamPaint = nvgImagePattern(VgContext, x, y, m_StreamWidth, 
            m_StreamHeight, 0.0f, m_ResourceId, 1.0f);
        nvgBeginPath(VgContext);
        nvgRect(VgContext, x, y, m_StreamWidth, m_StreamHeight);
        nvgFillPaint(VgContext, StreamPaint);
        nvgFill(VgContext);
    }

    // Adjust y again to point at the top of the window
    y += (m_Height - WINDOW_HEADER_HEIGHT);

	// Header
	nvgBeginPath(VgContext);
	nvgRoundedRectVarying(VgContext, x, y, m_Width, WINDOW_HEADER_HEIGHT, 0.0f, 0.0f, WINDOW_CORNER_RADIUS - 1, WINDOW_CORNER_RADIUS - 1);
	nvgFillColor(VgContext, m_Active ? nvgRGBA(WINDOW_HEADER_ACTIVE_RGBA) : nvgRGBA(WINDOW_HEADER_INACTIVE_RGBA));
	nvgFill(VgContext);
}
