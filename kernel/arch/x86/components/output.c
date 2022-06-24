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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
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
#include <arch/x86/vbe.h>
#include <heap.h>

static BootTerminal_t g_bootTerminal = { 0 };

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
    unsigned int clampedX  = MIN(X, (g_bootTerminal.Info.Width - 1));
    unsigned int clampedY  = MIN(Y, (g_bootTerminal.Info.Height - 1));
    size_t       offset    = (clampedY * g_bootTerminal.Info.BytesPerScanline) + (clampedX * (g_bootTerminal.Info.Depth / 8));
    uint32_t*    bbPointer = (uint32_t*)(g_bootTerminal.BackBufferAddress + offset);
    uint32_t*    fbPointer = (uint32_t*)(g_bootTerminal.FrameBufferAddress + offset);

    (*fbPointer) = (0xFF000000 | Color);
    if (g_bootTerminal.BackBufferAddress) {
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
    fbOffset  = (CursorY * g_bootTerminal.Info.BytesPerScanline) + (CursorX * (g_bootTerminal.Info.Depth / 8));
    fbPointer = (uint32_t*)(g_bootTerminal.FrameBufferAddress + fbOffset);
    bbPointer = (uint32_t*)(g_bootTerminal.BackBufferAddress + fbOffset);

    // If it's unicode lookup index
#ifdef UNICODE
    for (i = 0; i < MCoreFontNumChars; i++) {
        if (MCoreFontIndex[i] == (uint16_t)Character)
            break;
    }
    if (i == MCoreFontNumChars) {
        return OsNotExists;
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
            if (g_bootTerminal.BackBufferAddress) {
                bbPointer[i] = (bmpData >> (7 - i)) & 0x1 ? (0xFF000000 | FgColor) : (0xFF000000 | BgColor);
            }
        }

        _fb       = (uintptr_t)fbPointer;
        _fb      += g_bootTerminal.Info.BytesPerScanline;
        fbPointer = (uint32_t*)_fb;

        _bb       = (uintptr_t)bbPointer;
        _bb      += g_bootTerminal.Info.BytesPerScanline;
        bbPointer = (uint32_t*)_bb;
    }

    return OsOK;
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
    lines = (int)(g_bootTerminal.CursorLimitY - g_bootTerminal.CursorStartY);

    // Calculate the initial screen position
    if (g_bootTerminal.BackBufferAddress) {
        videoPointer = (uint8_t*)(g_bootTerminal.BackBufferAddress +
                                  ((g_bootTerminal.CursorStartY * g_bootTerminal.Info.BytesPerScanline)
                                   + (g_bootTerminal.CursorStartX * (g_bootTerminal.Info.Depth / 8))));
    }
    else {
        videoPointer = (uint8_t*)(g_bootTerminal.FrameBufferAddress +
                                  ((g_bootTerminal.CursorStartY * g_bootTerminal.Info.BytesPerScanline)
                                   + (g_bootTerminal.CursorStartX * (g_bootTerminal.Info.Depth / 8))));
    }

    // Calculate num of bytes
    bytesToCopy = ((g_bootTerminal.CursorLimitX - g_bootTerminal.CursorStartX)
                   * (g_bootTerminal.Info.Depth / 8));

    // Do the actual scroll, crappy routine since we have borders.... this means we can't do a
    // continous copy
    for (i = 0; i < lineCount; i++) {
        for (j = 0; j < lines; j++) {
            memcpy(
                    videoPointer,
                    videoPointer + (g_bootTerminal.Info.BytesPerScanline * MCoreFontHeight),
                    bytesToCopy
            );
            videoPointer += g_bootTerminal.Info.BytesPerScanline;
        }
    }

    // Clear out the lines that was scrolled
    videoPointer = (uint8_t*)(g_bootTerminal.FrameBufferAddress +
                              ((g_bootTerminal.CursorStartX * (g_bootTerminal.Info.Depth / 8))));

    // Scroll pointer down to bottom - n lines
    videoPointer += (g_bootTerminal.Info.BytesPerScanline
                     * (g_bootTerminal.CursorLimitY - (MCoreFontHeight * lineCount)));

    // Clear out lines
    for (i = 0; i < ((int)MCoreFontHeight * lineCount); i++) {
        memset(videoPointer, 0xFF, bytesToCopy);
        videoPointer += g_bootTerminal.Info.BytesPerScanline;
    }

    // We did the scroll, modify cursor
    g_bootTerminal.CursorY -= (MCoreFontHeight * lineCount);
    VideoFlush();
    return OsOK;
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
        g_bootTerminal.CursorX = g_bootTerminal.CursorStartX;
        g_bootTerminal.CursorY += MCoreFontHeight;
    } break;

    // Carriage Return
    // Reset X don't increase Y
    case '\r': {
        g_bootTerminal.CursorX = g_bootTerminal.CursorStartX;
    } break;

    // Default
    // Printable character
    default: {
        // Call print with the current location
        // and use the current colors
        VesaDrawCharacter(g_bootTerminal.CursorX, g_bootTerminal.CursorY,
                          Character, g_bootTerminal.FgColor, g_bootTerminal.BgColor);
        g_bootTerminal.CursorX += (MCoreFontWidth + 1);
    } break;
    }

    // Next step is to do some post-print
    // checks, including new-line and scroll-checks

    // Are we at last X position? - New-line
    if ((g_bootTerminal.CursorX + (MCoreFontWidth + 1)) >= g_bootTerminal.CursorLimitX) {
        g_bootTerminal.CursorX = g_bootTerminal.CursorStartX;
        g_bootTerminal.CursorY += MCoreFontHeight;
    }

    // Do we need to scroll the terminal?
    if ((g_bootTerminal.CursorY + MCoreFontHeight) >= g_bootTerminal.CursorLimitY) {
        VesaScroll(1);
    }
    return OsOK;
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
    Video = (uint16_t*)g_bootTerminal.FrameBufferAddress +
            (CursorY * g_bootTerminal.Info.Width + CursorX);

    // Plot it on the screen
    *Video = Data;

    return OsOK;
}

static OsStatus_t 
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
    return OsOK;
}

static OsStatus_t 
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
    return OsOK;
}

static void __SetTerminalMode(
        _In_ struct VBootVideo* video)
{
    g_bootTerminal.Info.Width                 = video->Width;
    g_bootTerminal.Info.Height                = video->Height;
    g_bootTerminal.Info.Depth                 = 16;
    g_bootTerminal.Info.BytesPerScanline      = 2 * video->Width;
    g_bootTerminal.FrameBufferAddress         = STD_VIDEO_MEMORY;
    g_bootTerminal.FrameBufferAddressPhysical = STD_VIDEO_MEMORY;

    g_bootTerminal.CursorLimitX = video->Width;
    g_bootTerminal.CursorLimitY = video->Height;
    g_bootTerminal.FgColor      = (0 << 4) | (15 & 0x0F);
    g_bootTerminal.BgColor      = 0;
}

static void __SetFramebufferMode(
        _In_ struct VBootVideo* video)
{
    g_bootTerminal.FrameBufferAddress         = video->FrameBuffer;
    g_bootTerminal.FrameBufferAddressPhysical = video->FrameBuffer;
    g_bootTerminal.Info.Width                 = video->Width;
    g_bootTerminal.Info.Height                = video->Height;
    g_bootTerminal.Info.Depth                 = (int)video->BitsPerPixel;
    g_bootTerminal.Info.BytesPerScanline      = video->Pitch;
    g_bootTerminal.Info.RedPosition           = (int)video->RedPosition;
    g_bootTerminal.Info.BluePosition          = (int)video->BluePosition;
    g_bootTerminal.Info.GreenPosition         = (int)video->GreenPosition;
    g_bootTerminal.Info.ReservedPosition      = (int)video->ReservedPosition;
    g_bootTerminal.Info.RedMask               = (int)video->RedMask;
    g_bootTerminal.Info.BlueMask              = (int)video->BlueMask;
    g_bootTerminal.Info.GreenMask             = (int)video->GreenMask;
    g_bootTerminal.Info.ReservedMask          = (int)video->ReservedMask;
    g_bootTerminal.CursorLimitX               = g_bootTerminal.Info.Width;
    g_bootTerminal.CursorLimitY               = (g_bootTerminal.Info.Height - MCoreFontHeight);
    g_bootTerminal.FgColor                    = 0xFFFFFFFF;
    g_bootTerminal.BgColor                    = 0xFF000000;
}

void
OutputInitialize(void)
{
    // Which kind of mode has been enabled for us
    if (GetMachine()->BootInformation.Video.FrameBuffer) {
        __SetFramebufferMode(&GetMachine()->BootInformation.Video);
    }
    else {
        // Either headless or terminal mode
        if (!GetMachine()->BootInformation.Video.Pitch) {
            return;
        }
        __SetTerminalMode(&GetMachine()->BootInformation.Video);
    }
}

BootTerminal_t*
VideoGetTerminal(void)
{
    return &g_bootTerminal;
}

static inline void memset32(uint32_t* out, uint32_t value, size_t byteCount)
{
    size_t count = byteCount >> 2;
    while (count) {
        *(out++) = value;
        count--;
    }
}

void
VideoClear(uint32_t color)
{
    if (g_bootTerminal.AvailableOutputs & (VIDEO_TEXT | VIDEO_GRAPHICS)) {
        size_t byteCount = g_bootTerminal.Info.BytesPerScanline * g_bootTerminal.Info.Height;

        if (g_bootTerminal.BackBufferAddress) {
            memset32((void*)g_bootTerminal.BackBufferAddress, color, byteCount);
        }
        memset32((void*)g_bootTerminal.FrameBufferAddress, color, byteCount);
    }
}

void
VideoFlush(void)
{
    if (g_bootTerminal.BackBufferAddress) {
        void*  source      = (void*)g_bootTerminal.BackBufferAddress;
        void*  destination = (void*)g_bootTerminal.FrameBufferAddress;
        size_t byteCount = g_bootTerminal.Info.BytesPerScanline * g_bootTerminal.Info.Height;
        memcpy(destination, source, byteCount);
    }
}

void
VideoDrawPixel(
    _In_ unsigned int X,
    _In_ unsigned int Y,
    _In_ uint32_t     Color)
{
    if (g_bootTerminal.AvailableOutputs & VIDEO_GRAPHICS) {
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
    if (g_bootTerminal.AvailableOutputs & VIDEO_TEXT) {
        return TextDrawCharacter(Character, Y, X, LOBYTE(LOWORD(g_bootTerminal.FgColor)));
    }
    else if (g_bootTerminal.AvailableOutputs & VIDEO_GRAPHICS) {
        return VesaDrawCharacter(X, Y, Character, Fg, Bg);
    }
    return OsNotSupported;
}

void
VideoPutCharacter(
    _In_ int character)
{
    // Start out by determining the kind of draw we want to do
    if (g_bootTerminal.AvailableOutputs & VIDEO_TEXT) {
        TextPutCharacter(character);
    }
    else if (g_bootTerminal.AvailableOutputs & VIDEO_GRAPHICS) {
        VesaPutCharacter(character);
    }
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

OsStatus_t
SerialPortInitialize(void)
{
    // Initalize the UART port (1)
#ifdef __OSCONFIG_DEBUGMODE
    g_bootTerminal.AvailableOutputs |= VIDEO_UART;
#endif
    return OsOK;
}

OsStatus_t
InitializeFramebufferOutput(void)
{
    // Which kind of mode has been enabled for us
    if (GetMachine()->BootInformation.Video.FrameBuffer) {
        size_t   backBufferSize = g_bootTerminal.Info.BytesPerScanline * g_bootTerminal.Info.Height;
        vaddr_t  backBuffer;
        int      pageCount = DIVUP(backBufferSize, GetMemorySpacePageSize());
        paddr_t* pages = kmalloc(sizeof(paddr_t) * pageCount);

        if (pages) {
            OsStatus_t status = MemorySpaceMap(
                    GetCurrentMemorySpace(),
                    &backBuffer,
                    &pages[0],
                    backBufferSize,
                    0,
                    MAPPING_COMMIT,
                    MAPPING_VIRTUAL_GLOBAL
            );
            if (status == OsOK) {
                g_bootTerminal.BackBufferAddress = backBuffer;
            }
            kfree(pages);
        }

        g_bootTerminal.AvailableOutputs |= VIDEO_GRAPHICS;
    }
    else {
        // Either headless or terminal mode
        if (GetMachine()->BootInformation.Video.Pitch) {
            g_bootTerminal.AvailableOutputs |= VIDEO_TEXT;
        }
    }
    return OsOK;
}
