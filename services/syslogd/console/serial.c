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

void
SerialPutCharacter(
        _In_ int character)
{
    size_t lineStatus      = 0x0;
    size_t characterBuffer = (size_t)(character & 0xFF);

    if (!(g_bootTerminal.AvailableOutputs & VIDEO_UART)) {
        return;
    }

    while (!(lineStatus & 0x20)) {
        ReadDirectIo(DeviceIoPortBased, 0x3F8 + 5, 1, &lineStatus);
    }
    WriteDirectIo(DeviceIoPortBased, 0x3F8, 1, characterBuffer);
}
