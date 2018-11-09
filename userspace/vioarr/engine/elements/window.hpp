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
#pragma once
#include "../entity.hpp"
#include <os/buffer.h>
#include <string>

class CLabel;

// Window Settings
// Adjustable window layout settings
#define WINDOW_CORNER_RADIUS                5.0f
#define WINDOW_HEADER_HEIGHT                30.0f

#define WINDOW_FILL_COLOR_RGBA              229, 229, 232, 255
#define WINDOW_HEADER_ACTIVE_RGBA           103, 103, 103, 255
#define WINDOW_HEADER_INACTIVE_RGBA         255, 255, 255, 8

class CWindow : public CEntity {
public:
    CWindow(CEntity* Parent, NVGcontext* VgContext, const std::string &Title, int Width, int Height);
    CWindow(NVGcontext* VgContext, const std::string &Title, int Width, int Height);
    CWindow(CEntity* Parent, NVGcontext* VgContext);
    CWindow(NVGcontext* VgContext);
    ~CWindow();

    void SetWidth(int Width);
    void SetHeight(int Height);
    void SetTitle(const std::string &Title);

    void SetStreamingBufferFormat(GLenum Format, GLenum InternalFormat);
    void SetStreamingBufferDimensions(int Width, int Height);
    void SetStreamingBuffer(DmaBuffer_t* Buffer);
    void SetStreaming(bool Enable);

    // Override inheritted methods
    void HandleKeyEvent(SystemKey_t* Key);

protected:
    // Override the inherited methods
    void Draw(NVGcontext* VgContext);
    void Update();

private:
    // Window information
    std::string     m_Title;
    int             m_Width;
    int             m_Height;
    bool            m_Streaming;
    CLabel*         m_TitleLabel;

    // Streaming support
    int             m_ResourceId;
    GLenum          m_Format;
    GLenum          m_InternalFormat;
    int             m_StreamWidth;
    int             m_StreamHeight;
    DmaBuffer_t*    m_StreamBuffer;
};
