/**
 * MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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
 * System Output Implementation
 *   - Provides ability to write text to either screen (text, drawing) and
 *     funnels all logging out to com ports
 */

#include <machine.h>
#include <arch/output.h>
#include <arch/io.h>
#include <string.h>
#include <vbe.h>
#include <heap.h>

static BootTerminal_t Terminal = { 0 };

void VideoFlush(void);

extern const uint8_t  MCoreFontBitmaps[];
extern const uint32_t MCoreFontNumChars;
extern const uint32_t MCoreFontHeight;
extern const uint32_t MCoreFontWidth;

#ifdef UNICODE
extern const uint16_t MCoreFontIndex[];
#endif

static void
VesaDrawPixel(
    _In_ unsigned X, 
    _In_ unsigned Y, 
    _In_ uint32_t Color)
{
    unsigned int clampedX  = MIN(X, (Terminal.Info.Width - 1));
    unsigned int clampedY  = MIN(Y, (Terminal.Info.Height - 1));
    size_t       offset    = (clampedY * Terminal.Info.BytesPerScanline) + (clampedX * (Terminal.Info.Depth / 8));
    uint32_t*    bbPointer = (uint32_t*)(Terminal.BackBufferAddress + offset);
    uint32_t*    fbPointer = (uint32_t*)(Terminal.FrameBufferAddress + offset);

    (*fbPointer) = (0xFF000000 | Color);
    if (Terminal.BackBufferAddress) {
        (*bbPointer) = (0xFF000000 | Color);
    }
}

static OsStatus_t 
VesaDrawCharacter(
    _In_ unsigned CursorX,
    _In_ unsigned CursorY,
    _In_ int      Character,
    _In_ uint32_t FgColor, 
    _In_ uint32_t BgColor)
{
    uint32_t*    fbPointer;
    uint32_t*    bbPointer;
    uint8_t*     charData;
    size_t       fbOffset;
    unsigned int row, i;

    // Calculate the video-offset
    fbOffset  = (CursorY * Terminal.Info.BytesPerScanline) + (CursorX * (Terminal.Info.Depth / 8));
    fbPointer = (uint32_t*)(Terminal.FrameBufferAddress + fbOffset);
    bbPointer = (uint32_t*)(Terminal.BackBufferAddress + fbOffset);

    // If it's unicode lookup index
#ifdef UNICODE
    for (i = 0; i < MCoreFontNumChars; i++) {
        if (MCoreFontIndex[i] == (uint16_t)Character)
            break;
    }
    if (i == MCoreFontNumChars) {
        return OsDoesNotExist;
    }
#else
    i = (unsigned)Character
#endif

    // Lookup bitmap
    charData = (uint8_t*)&MCoreFontBitmaps[i * MCoreFontHeight];

    // Iterate bitmap rows
    for (row = 0; row < MCoreFontHeight; row++) {
        uint8_t   bmpData = charData[row];
        uintptr_t _fb, _bb;

        // Render data in row
        for (i = 0; i < 8; i++) {
            fbPointer[i] = (bmpData >> (7 - i)) & 0x1 ? (0xFF000000 | FgColor) : (0xFF000000 | BgColor);
            if (Terminal.BackBufferAddress) {
                bbPointer[i] = (bmpData >> (7 - i)) & 0x1 ? (0xFF000000 | FgColor) : (0xFF000000 | BgColor);
            }
        }

        _fb       = (uintptr_t)fbPointer;
        _fb      += Terminal.Info.BytesPerScanline;
        fbPointer = (uint32_t*)_fb;

        _bb       = (uintptr_t)bbPointer;
        _bb      += Terminal.Info.BytesPerScanline;
        bbPointer = (uint32_t*)_bb;
    }

    return OsSuccess;
}

static OsStatus_t 
VesaScroll(
    _In_ int lineCount)
{
    uint8_t* videoPointer;
    size_t   bytesToCopy;
    int      lines;
    int      i, j;

    // How many lines do we need to modify?
    lines = (Terminal.CursorLimitY - Terminal.CursorStartY);

    // Calculate the initial screen position
    if (Terminal.BackBufferAddress) {
        videoPointer = (uint8_t*)(Terminal.BackBufferAddress +
                                  ((Terminal.CursorStartY * Terminal.Info.BytesPerScanline)
                                   + (Terminal.CursorStartX * (Terminal.Info.Depth / 8))));
    }
    else {
        videoPointer = (uint8_t*)(Terminal.FrameBufferAddress +
                                  ((Terminal.CursorStartY * Terminal.Info.BytesPerScanline)
                                   + (Terminal.CursorStartX * (Terminal.Info.Depth / 8))));
    }

    // Calculate num of bytes
    bytesToCopy = ((Terminal.CursorLimitX - Terminal.CursorStartX)
                   * (Terminal.Info.Depth / 8));

    // Do the actual scroll, crappy routine since we have borders.... this means we can't do a
    // continous copy
    for (i = 0; i < lineCount; i++) {
        for (j = 0; j < lines; j++) {
            memcpy(videoPointer, videoPointer +
                                 (Terminal.Info.BytesPerScanline * MCoreFontHeight), bytesToCopy);
            videoPointer += Terminal.Info.BytesPerScanline;
        }
    }

    // Clear out the lines that was scrolled
    videoPointer = (uint8_t*)(Terminal.FrameBufferAddress +
                              ((Terminal.CursorStartX * (Terminal.Info.Depth / 8))));

    // Scroll pointer down to bottom - n lines
    videoPointer += (Terminal.Info.BytesPerScanline
                     * (Terminal.CursorLimitY - (MCoreFontHeight * lineCount)));

    // Clear out lines
    for (i = 0; i < ((int)MCoreFontHeight * lineCount); i++) {
        memset(videoPointer, 0xFF, bytesToCopy);
        videoPointer += Terminal.Info.BytesPerScanline;
    }

    // We did the scroll, modify cursor
    Terminal.CursorY -= (MCoreFontHeight * lineCount);

    if (Terminal.BackBufferAddress) {
        VideoFlush();
    }
    return OsSuccess;
}

static OsStatus_t 
VesaPutCharacter(
    _In_ int Character)
{
    // The first step is to handle special
    // case characters that we shouldn't print out
    switch (Character) 
    {
        // New-Line
        // Reset X and increase Y
    case '\n': {
        Terminal.CursorX = Terminal.CursorStartX;
        Terminal.CursorY += MCoreFontHeight;
    } break;

    // Carriage Return
    // Reset X don't increase Y
    case '\r': {
        Terminal.CursorX = Terminal.CursorStartX;
    } break;

    // Default
    // Printable character
    default: {
        // Call print with the current location
        // and use the current colors
        VesaDrawCharacter(Terminal.CursorX, Terminal.CursorY,
            Character, Terminal.FgColor, Terminal.BgColor);
        Terminal.CursorX += (MCoreFontWidth + 1);
    } break;
    }

    // Next step is to do some post-print
    // checks, including new-line and scroll-checks

    // Are we at last X position? - New-line
    if ((Terminal.CursorX + (MCoreFontWidth + 1)) >= Terminal.CursorLimitX) {
        Terminal.CursorX = Terminal.CursorStartX;
        Terminal.CursorY += MCoreFontHeight;
    }

    // Do we need to scroll the terminal?
    if ((Terminal.CursorY + MCoreFontHeight) >= Terminal.CursorLimitY) {
        VesaScroll(1);
    }
    return OsSuccess;
}

static OsStatus_t 
TextDrawCharacter(
    _In_ int      Character,
    _In_ unsigned CursorY,
    _In_ unsigned CursorX,
    _In_ uint8_t  Color)
{
    uint16_t* Video = NULL;
    uint16_t  Data = ((uint16_t)Color << 8) | (uint8_t)(Character & 0xFF);

    // Calculate video position
    Video = (uint16_t*)Terminal.FrameBufferAddress +
        (CursorY * Terminal.Info.Width + CursorX);

    // Plot it on the screen
    *Video = Data;

    return OsSuccess;
}

static OsStatus_t 
TextScroll(
    _In_ int ByLines)
{
    // Variables
    uint16_t *Video = (uint16_t*)Terminal.FrameBufferAddress;
    uint16_t Color = (uint16_t)(Terminal.FgColor << 8);
    unsigned i;
    int j;

    // Move display n lines up
    for (j = 0; j < ByLines; j++) {
        for (i = 0; i < (Terminal.Info.Height - 1) * Terminal.Info.Width;
            i++) {
            Video[i] = Video[i + Terminal.Info.Width];
        }

        // Clear last line
        for (i = ((Terminal.Info.Height - 1) * Terminal.Info.Width);
            i < (Terminal.Info.Height * Terminal.Info.Width);
            i++) {
            Video[i] = (uint16_t)(Color | ' ');
        }
    }

    // Update new Y cursor position
    Terminal.CursorY = (Terminal.Info.Height - ByLines);

    // Done - no errors
    return OsSuccess;
}

static OsStatus_t 
TextPutCharacter(
    _In_ int Character)
{
    // Variables
    uint16_t CursorLoc = 0;

    // Special case characters
    // Backspace
    if (Character == 0x08 && Terminal.CursorX)
        Terminal.CursorX--;

    // Tab
    else if (Character == 0x09)
        Terminal.CursorX = ((Terminal.CursorX + 8) & ~(8 - 1));

    // Carriage Return
    else if (Character == '\r')
        Terminal.CursorX = 0;

    // New Line
    else if (Character == '\n') {
        Terminal.CursorX = 0;
        Terminal.CursorY++;
    }
    
    // Printable characters
    else if (Character >= ' ') {
        TextDrawCharacter(Character, Terminal.CursorY, 
            Terminal.CursorX, LOBYTE(LOWORD(Terminal.FgColor)));
        Terminal.CursorX++;
    }

    // Go to new line?
    if (Terminal.CursorX >= Terminal.Info.Width) {
        Terminal.CursorX = 0;
        Terminal.CursorY++;
    }

    // Scroll if at last line
    if (Terminal.CursorY >= Terminal.Info.Height) {
        TextScroll(1);
    }

    // Update HW Cursor
    CursorLoc = (uint16_t)((Terminal.CursorY * Terminal.Info.Width) 
        + Terminal.CursorX);

    // Send the high byte.
    WriteDirectIo(DeviceIoPortBased, 0x3D4, 1, 14);
    WriteDirectIo(DeviceIoPortBased, 0x3D5, 1, (uint8_t)(CursorLoc >> 8));

    // Send the low byte.
    WriteDirectIo(DeviceIoPortBased, 0x3D4, 1, 15);
    WriteDirectIo(DeviceIoPortBased, 0x3D5, 1, (uint8_t)CursorLoc);
    return OsSuccess;
}

// Unfortunately we have to do the initial parsing before we disable
// access to the first memory page where this data is located. And this
// has to be done before create the memory mappings as the virtual paging
// system has to be made aware of the location of the framebuffer.
void
VbeInitialize(void)
{
    // Which kind of mode has been enabled for us
    switch (GetMachine()->BootInformation.VbeMode) {
        // Headless
        case 0: {
        } break;

        // Text-Mode (80x25)
        case 1: {
            Terminal.Info.Width                 = 80;
            Terminal.Info.Height                = 25;
            Terminal.Info.Depth                 = 16;
            Terminal.Info.BytesPerScanline      = 2 * 80;
            Terminal.FrameBufferAddress         = STD_VIDEO_MEMORY;
            Terminal.FrameBufferAddressPhysical = STD_VIDEO_MEMORY;

            Terminal.CursorLimitX = 80;
            Terminal.CursorLimitY = 25;
            Terminal.FgColor      = (0 << 4) | (15 & 0x0F);
            Terminal.BgColor      = 0;
        } break;

        // Text-Mode (80x50)
        case 2: {
            Terminal.Info.Width                 = 80;
            Terminal.Info.Height                = 50;
            Terminal.Info.Depth                 = 16;
            Terminal.Info.BytesPerScanline      = 2 * 80;
            Terminal.FrameBufferAddress         = STD_VIDEO_MEMORY;
            Terminal.FrameBufferAddressPhysical = STD_VIDEO_MEMORY;

            Terminal.CursorLimitX = 80;
            Terminal.CursorLimitY = 50;
            Terminal.FgColor      = (0 << 4) | (15 & 0x0F);
            Terminal.BgColor      = 0;
        } break;

        // VGA-Mode (Graphics)
        case 3: {

            Terminal.CursorLimitX = Terminal.Info.Width / (MCoreFontWidth + 1);
            Terminal.CursorLimitY = Terminal.Info.Height / MCoreFontHeight;
            Terminal.FgColor      = (0 << 4) | (15 & 0x0F);
            Terminal.BgColor      = 0;
        } break;

        // VBE-Mode (Graphics)
        default: {
            if (GetMachine()->BootInformation.VbeModeInfo) {
                VbeMode_t* VbeModePointer = (VbeMode_t*)(uintptr_t)GetMachine()->BootInformation.VbeModeInfo;
                
                Terminal.FrameBufferAddress         = VbeModePointer->PhysBasePtr;
                Terminal.FrameBufferAddressPhysical = VbeModePointer->PhysBasePtr;
                Terminal.Info.Width                 = VbeModePointer->XResolution;
                Terminal.Info.Height                = VbeModePointer->YResolution;
                Terminal.Info.Depth                 = VbeModePointer->BitsPerPixel;
                Terminal.Info.BytesPerScanline      = VbeModePointer->BytesPerScanLine;
                Terminal.Info.RedPosition           = VbeModePointer->RedMaskPos;
                Terminal.Info.BluePosition          = VbeModePointer->BlueMaskPos;
                Terminal.Info.GreenPosition         = VbeModePointer->GreenMaskPos;
                Terminal.Info.ReservedPosition      = VbeModePointer->ReservedMaskPos;
                Terminal.Info.RedMask               = VbeModePointer->RedMaskSize;
                Terminal.Info.BlueMask              = VbeModePointer->BlueMaskSize;
                Terminal.Info.GreenMask             = VbeModePointer->GreenMaskSize;
                Terminal.Info.ReservedMask          = VbeModePointer->ReservedMaskSize;
                Terminal.CursorLimitX               = Terminal.Info.Width;
                Terminal.CursorLimitY               = Terminal.Info.Height;
                Terminal.FgColor                    = 0;
                Terminal.BgColor                    = 0xFFFFFFFF;
            }
        } break;
    }
}

BootTerminal_t*
VideoGetTerminal(void)
{
    return &Terminal;
}

void
VideoClear(void)
{
    if (Terminal.AvailableOutputs & (VIDEO_TEXT | VIDEO_GRAPHICS)) {
        size_t byteCount = Terminal.Info.BytesPerScanline * Terminal.Info.Height;

        if (Terminal.BackBufferAddress) {
            memset((void*)Terminal.BackBufferAddress, 0xFF, byteCount);
        }
        memset((void*)Terminal.FrameBufferAddress, 0xFF, byteCount);
    }
}

void
VideoFlush(void)
{
    if (Terminal.BackBufferAddress) {
        void*  source      = (void*)Terminal.BackBufferAddress;
        void*  destination = (void*)Terminal.FrameBufferAddress;
        size_t byteCount = Terminal.Info.BytesPerScanline * Terminal.Info.Height;
        memcpy(destination, source, byteCount);
    }
}

void
VideoDrawPixel(
    _In_ unsigned int X,
    _In_ unsigned int Y,
    _In_ uint32_t     Color)
{
    if (Terminal.AvailableOutputs & VIDEO_GRAPHICS) {
        VesaDrawPixel(X, Y, Color);
    }
}

OsStatus_t
VideoDrawCharacter(
    _In_ unsigned int X,
    _In_ unsigned int Y,
    _In_ int          Character,
    _In_ uint32_t     Bg,
    _In_ uint32_t     Fg)
{
    if (Terminal.AvailableOutputs & VIDEO_TEXT) {
        return TextDrawCharacter(Character, Y, X, LOBYTE(LOWORD(Terminal.FgColor)));
    }
    else if (Terminal.AvailableOutputs & VIDEO_GRAPHICS) {
        return VesaDrawCharacter(X, Y, Character, Fg, Bg);
    }
    return OsNotSupported;
}

void
VideoPutCharacter(
    _In_ int character)
{
    // Start out by determining the kind of draw we want to do
    if (Terminal.AvailableOutputs & VIDEO_TEXT) {
        TextPutCharacter(character);
    }
    else if (Terminal.AvailableOutputs & VIDEO_GRAPHICS) {
        VesaPutCharacter(character);
    }
}

void
SerialPutCharacter(
        _In_ int character)
{
    size_t lineStatus      = 0x0;
    size_t characterBuffer = (size_t)(character & 0xFF);

    if (!(Terminal.AvailableOutputs & VIDEO_UART)) {
        return;
    }

    while (!(lineStatus & 0x20)) {
        ReadDirectIo(DeviceIoPortBased, 0x3F8 + 5, 1, &lineStatus);
    }
    WriteDirectIo(DeviceIoPortBased, 0x3F8, 1, characterBuffer);
}

OsStatus_t
InitializeSerialOutput(void)
{
    // Initalize the UART port (1)
#ifdef __OSCONFIG_DEBUGMODE
    Terminal.AvailableOutputs |= VIDEO_UART;
#endif
    return OsSuccess;
}

OsStatus_t
InitializeFramebufferOutput(void)
{
    // Which kind of mode has been enabled for us
    switch (GetMachine()->BootInformation.VbeMode) {
        // Headless
        case 0: {
        } break;

        // Text-Mode (80x25)
        case 1: {
            Terminal.AvailableOutputs |= VIDEO_TEXT;
        } break;

        // Text-Mode (80x50)
        case 2: {
            Terminal.AvailableOutputs |= VIDEO_TEXT;
        } break;

        // VGA-Mode (Graphics)
        case 3: {
            Terminal.AvailableOutputs |= VIDEO_GRAPHICS;
        } break;

        // VBE-Mode (Graphics)
        default: {
            if (GetMachine()->BootInformation.VbeModeInfo) {
                size_t  backBufferSize = Terminal.Info.BytesPerScanline * Terminal.Info.Height;
                vaddr_t backBuffer;
                int     pageCount = DIVUP(backBufferSize, GetMemorySpacePageSize());
                paddr_t* pages = kmalloc(sizeof(paddr_t) * pageCount);

                if (pages) {
                    OsStatus_t status = MemorySpaceMap(GetCurrentMemorySpace(), &backBuffer,
                                                             &pages[0], backBufferSize,
                                                             MAPPING_COMMIT, MAPPING_VIRTUAL_GLOBAL);
                    if (status == OsSuccess) {
                        Terminal.BackBufferAddress = backBuffer;
                    }
                    kfree(pages);
                }

                Terminal.AvailableOutputs |= VIDEO_GRAPHICS;
            }
        } break;
    }
    return OsSuccess;
}
