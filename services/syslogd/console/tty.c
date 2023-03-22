/**
 * Copyright 2023, Philip Meulengracht
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <ddk/video.h>

struct Screen {
    VideoDescriptor_t Info;
};

static struct Screen* g_screen = NULL;

void ScreenInitialize(void)
{

}

static oserr_t
TextDrawCharacter(
        _In_ int      Character,
        _In_ unsigned CursorY,
        _In_ unsigned CursorX,
        _In_ uint8_t  Color)
{
    uint16_t* Video = NULL;
    uint16_t  Data = ((uint16_t)Color << 8) | (uint8_t)(Character & 0xFF);

    // Calculate video position
    Video = (uint16_t*)g_bootTerminal.FrameBufferAddress +
            (CursorY * g_bootTerminal.Info.Width + CursorX);

    // Plot it on the screen
    *Video = Data;

    return OS_EOK;
}

static oserr_t
TextScroll(
        _In_ int ByLines)
{
    // Variables
    uint16_t *Video = (uint16_t*)g_bootTerminal.FrameBufferAddress;
    uint16_t Color = (uint16_t)(g_bootTerminal.FgColor << 8);
    unsigned i;
    int j;

    // Move display n lines up
    for (j = 0; j < ByLines; j++) {
        for (i = 0; i < (g_bootTerminal.Info.Height - 1) * g_bootTerminal.Info.Width;
             i++) {
            Video[i] = Video[i + g_bootTerminal.Info.Width];
        }

        // Clear last line
        for (i = ((g_bootTerminal.Info.Height - 1) * g_bootTerminal.Info.Width);
             i < (g_bootTerminal.Info.Height * g_bootTerminal.Info.Width);
             i++) {
            Video[i] = (uint16_t)(Color | ' ');
        }
    }

    // Update new Y cursor position
    g_bootTerminal.CursorY = (g_bootTerminal.Info.Height - ByLines);

    // Done - no errors
    return OS_EOK;
}

static oserr_t
TextPutCharacter(
        _In_ int Character)
{
    // Variables
    uint16_t CursorLoc = 0;

    // Special case characters
    // Backspace
    if (Character == 0x08 && g_bootTerminal.CursorX)
        g_bootTerminal.CursorX--;

        // Tab
    else if (Character == 0x09)
        g_bootTerminal.CursorX = ((g_bootTerminal.CursorX + 8) & ~(8 - 1));

        // Carriage Return
    else if (Character == '\r')
        g_bootTerminal.CursorX = 0;

        // New Line
    else if (Character == '\n') {
        g_bootTerminal.CursorX = 0;
        g_bootTerminal.CursorY++;
    }

        // Printable characters
    else if (Character >= ' ') {
        TextDrawCharacter(Character, g_bootTerminal.CursorY,
                          g_bootTerminal.CursorX, LOBYTE(LOWORD(g_bootTerminal.FgColor)));
        g_bootTerminal.CursorX++;
    }

    // Go to new line?
    if (g_bootTerminal.CursorX >= g_bootTerminal.Info.Width) {
        g_bootTerminal.CursorX = 0;
        g_bootTerminal.CursorY++;
    }

    // Scroll if at last line
    if (g_bootTerminal.CursorY >= g_bootTerminal.Info.Height) {
        TextScroll(1);
    }

    // Update HW Cursor
    CursorLoc = (uint16_t)((g_bootTerminal.CursorY * g_bootTerminal.Info.Width)
                           + g_bootTerminal.CursorX);

    // Send the high byte.
    WriteDirectIo(DeviceIoPortBased, 0x3D4, 1, 14);
    WriteDirectIo(DeviceIoPortBased, 0x3D5, 1, (uint8_t)(CursorLoc >> 8));

    // Send the low byte.
    WriteDirectIo(DeviceIoPortBased, 0x3D4, 1, 15);
    WriteDirectIo(DeviceIoPortBased, 0x3D5, 1, (uint8_t)CursorLoc);
    return OS_EOK;
}
