/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * Video Interface (Boot)
 * - Contains the shared kernel video interface
 *   that all sub-layers / architectures must conform to
 */

#ifndef __VALI_OUTPUT_H__
#define __VALI_OUTPUT_H__

#include <os/osdefs.h>
#include <ddk/video.h>

typedef struct BootTerminal {
    unsigned int				AvailableOutputs;
    VideoDescriptor_t			Info;

    uintptr_t                   FrameBufferAddress;
    uintptr_t                   FrameBufferAddressPhysical;
    uintptr_t                   BackBufferAddress;

    unsigned					CursorX;
    unsigned					CursorY;
    
    unsigned					CursorStartX;
    unsigned					CursorStartY;
    unsigned					CursorLimitX;
    unsigned					CursorLimitY;

    uint32_t					FgColor;
    uint32_t					BgColor;
} BootTerminal_t;

/* Video Type Definitions
 * Currently only two kind of video types needs
 * to be supported for boot - textmode or lfbmode */
#define VIDEO_UART      0x00000001
#define VIDEO_TEXT      0x00000002
#define VIDEO_GRAPHICS  0x00000004 // This and _TEXT are mutually exclusive

/* VideoGetTerminal
 * Retrieves the current terminal information */
KERNELAPI BootTerminal_t* KERNELABI VideoGetTerminal(void);

/* VideoClear
 * Clears the video framebuffer by initializing it to a default color. */
KERNELAPI void KERNELABI VideoClear(uint32_t color);

/* VideoDrawPixel
 * Draws a pixel of the given color at the specifiedpixel-position */
KERNELAPI void KERNELABI
VideoDrawPixel(
    _In_ unsigned int X, 
    _In_ unsigned int Y, 
    _In_ uint32_t     Color);

/* VideoDrawCharacter
 * Renders a character of the given color(s) at the specified pixel-position */
KERNELAPI OsStatus_t KERNELABI
VideoDrawCharacter(
    _In_ unsigned int X, 
    _In_ unsigned int Y, 
    _In_ int          Character, 
    _In_ uint32_t     Bg, 
    _In_ uint32_t     Fg);

/**
 * Displays a character to the default video output if available. Default colors will be used
 * @param character [In] The character to display
 * @return
 */
KERNELAPI void KERNELABI
VideoPutCharacter(
    _In_ int character);

/**
 * Outputs a character to the default serial port if available.
 * @param character [In] The character to write
 */
void
SerialPutCharacter(
        _In_ int character);

#endif //!__VALI_OUTPUT_H__
