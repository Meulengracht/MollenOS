/**
 * Copyright 2021, Philip Meulengracht
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
 */

#include <library.h>
#include <console.h>
#include <video.h>

#include <Protocol/GraphicsOutput.h>

#define MAX_VIDEO_MODE_COUNT 32

struct VideoModeComponent {
    unsigned int Position;
    unsigned int Mask;
};

struct VideoMode {
    int ModeNumber;
    int Width;
    int Height;
    int BitsPerPixel;
    int BytesPerScanline;

    struct VideoModeComponent Red;
    struct VideoModeComponent Green;
    struct VideoModeComponent Blue;
    struct VideoModeComponent Reserved;
};

static EFI_GUID gGraphicsOutputProtocolGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
static EFI_GRAPHICS_OUTPUT_PROTOCOL* gGraphicsOutput = NULL;

static struct PrefferedMode { int Width; int Height; int BitsPerPixel; } gPrefferedModes[] = {
    //{ 8192, 4320, 32 },
    //{ 7680, 4320, 32 },
    //{ 5120, 2880, 32 },
    //{ 4096, 2160, 32 },
    //{ 3840, 2160, 32 },
    //{ 2560, 1440, 32 },
    //{ 1920, 1080, 32 },
    //{ 1600, 1200, 32 },
    { 1280, 1024, 32 },
    { 1024, 768, 32 },
    { 640, 480, 32 },
    { 800, 600, 32 },
    { 0, 0, 0 }
};

void __ConvertColorMask(
    IN  UINT32                     Value,
    OUT struct VideoModeComponent* Component)
{
    UINT32 Index = 0;
    UINT32 Mask = 1;

    if (!Value) {
        Component->Position = 0;
        Component->Mask     = 0;
        return;
    }

    while ((Index < 32) && ((Value & Mask) == 0)) {
        Index++;
        Mask <<= 1;
    }
    
    Component->Position = Index;
    Component->Mask     = (Mask >> Index);
}

int __GetHighestBitSet(UINT32 Value)
{
    int Count = 0;
    while (Value) {
        Count++;
        Value >>= 1;
    }
    return Count;
}

int __GetPixelBitCount(EFI_PIXEL_BITMASK* Pixel)
{
    return MAX(
        __GetHighestBitSet(Pixel->RedMask), 
        MAX(
            __GetHighestBitSet(Pixel->GreenMask), 
            MAX(__GetHighestBitSet(Pixel->BlueMask), 
                __GetHighestBitSet(Pixel->ReservedMask)
            )
        )
    );
}

EFI_STATUS __ParseVideoFormat(
    IN EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* GopModeInfo,
    IN struct VideoMode*                     Mode)
{
    Mode->Width = GopModeInfo->HorizontalResolution;
    Mode->Height = GopModeInfo->VerticalResolution;

    switch (GopModeInfo->PixelFormat) {
        case PixelRedGreenBlueReserved8BitPerColor:
            Mode->BitsPerPixel = 32;
            Mode->Red.Position = 16;
            Mode->Red.Mask = 0xFF;
            Mode->Green.Position = 8;
            Mode->Green.Mask = 0xFF;
            Mode->Blue.Position = 0;
            Mode->Blue.Mask = 0xFF;
            Mode->Reserved.Position = 24;
            Mode->Reserved.Mask = 0xFF;
            Mode->BytesPerScanline = GopModeInfo->PixelsPerScanLine * 4;
            break;
        case PixelBlueGreenRedReserved8BitPerColor:
            Mode->BitsPerPixel = 32;
            Mode->Red.Position = 0;
            Mode->Red.Mask = 0xFF;
            Mode->Green.Position = 8;
            Mode->Green.Mask = 0xFF;
            Mode->Blue.Position = 16;
            Mode->Blue.Mask = 0xFF;
            Mode->Reserved.Position = 24;
            Mode->Reserved.Mask = 0xFF;
            Mode->BytesPerScanline = GopModeInfo->PixelsPerScanLine * 4;
            break;
        case PixelBitMask:
            __ConvertColorMask(GopModeInfo->PixelInformation.RedMask, &Mode->Red);
            __ConvertColorMask(GopModeInfo->PixelInformation.GreenMask, &Mode->Green);
            __ConvertColorMask(GopModeInfo->PixelInformation.BlueMask, &Mode->Blue);
            __ConvertColorMask(GopModeInfo->PixelInformation.ReservedMask, &Mode->Reserved);
            Mode->BitsPerPixel = __GetPixelBitCount(&GopModeInfo->PixelInformation);
            Mode->BytesPerScanline = GopModeInfo->PixelsPerScanLine * (Mode->BitsPerPixel / 8);
            break;
        default:
            return EFI_UNSUPPORTED;
    }
    return EFI_SUCCESS;
}

EFI_STATUS __GetVideoModes(
    IN  EFI_GRAPHICS_OUTPUT_PROTOCOL* Gop,
    IN  struct VideoMode*             Modes,
    IN  UINT32                        MaxCount,
    OUT UINT32*                       Count)
{
	EFI_STATUS                            Status;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* GopModeInfo;
    UINTN                                 SizeOfInfo;
    UINT32                                i, j;
    ConsoleWrite(L"__GetVideoModes()\n");

    for (i = 0, j = 0; j < MaxCount; i++) {
        Status = Gop->QueryMode(Gop, i, &SizeOfInfo, &GopModeInfo);
        if (EFI_ERROR(Status)) {
            break;
        }

        // Store the mode number so we can set it later
        Modes[j].ModeNumber = i;

        Status = __ParseVideoFormat(GopModeInfo, &Modes[j]);
        if (EFI_ERROR(Status)) {
            continue;
        }
        j++;
    }

    *Count = j;
    return (j != 0) ? EFI_SUCCESS : EFI_UNSUPPORTED;
}

struct VideoMode* __GetPrefferedMode(
    struct VideoMode* Modes,
    UINT32            ModeCount)
{
    UINT32 i = 0;
    while (gPrefferedModes[i].Width != 0) {
        for (UINT32 j = 0; j < ModeCount; j++) {
            if ((Modes[j].Width == gPrefferedModes[i].Width) &&
                (Modes[j].Height == gPrefferedModes[i].Height)) {
                return &Modes[j];
            }
        }
    }
    return NULL;
}

EFI_STATUS __DefaultToTextMode(
    IN struct VBoot* VBoot)
{
    VBoot->Video.Width = 80;
    VBoot->Video.Height = 25;
    VBoot->Video.BitsPerPixel = 8;
    VBoot->Video.Pitch = VBoot->Video.Width;
    VBoot->Video.FrameBuffer = 0;
    VBoot->Video.RedPosition = 0;
    VBoot->Video.RedMask = 0;
    VBoot->Video.GreenPosition = 0;
    VBoot->Video.GreenMask = 0;
    VBoot->Video.BluePosition = 0;
    VBoot->Video.BlueMask = 0;
    VBoot->Video.ReservedPosition = 0;
    VBoot->Video.ReservedMask = 0;

    return EFI_SUCCESS;
}

EFI_STATUS VideoInitialize(
    IN struct VBoot* VBoot)
{
	EFI_STATUS        Status;
    UINT32            ModeCount;
    struct VideoMode* Modes;
    struct VideoMode* PrefferedMode;
    ConsoleWrite(L"VideoInitialize()\n");

    Status = gBootServices->LocateProtocol(
        &gGraphicsOutputProtocolGuid,
        NULL,
        (VOID**)&gGraphicsOutput
    );
    if (EFI_ERROR(Status)) {
        ConsoleWrite(L"VideoInitialize no gop present, defaulting to text mode\n");
        return __DefaultToTextMode(VBoot);
    }

    Status = LibraryAllocateMemory(
        sizeof(struct VideoMode) * MAX_VIDEO_MODE_COUNT,
        (VOID**)&Modes
    );
    if (EFI_ERROR(Status)) {
        ConsoleWrite(L"VideoInitialize failed to allocate memory\n");
        return Status;
    }

    Status = __GetVideoModes(gGraphicsOutput, Modes, MAX_VIDEO_MODE_COUNT, &ModeCount);
    if (EFI_ERROR(Status)) {
        goto exit;
    }

    // Try list of modes we would like, in order of preference
    PrefferedMode = __GetPrefferedMode(Modes, ModeCount);
    if (!PrefferedMode) {
        // If we can't find a mode, just use the first one
        goto exit;
    }

    ConsoleWrite(L"Setting video mode to %dx%dx%d\n", 
        PrefferedMode->Width, PrefferedMode->Height, PrefferedMode->BitsPerPixel);
    Status = gGraphicsOutput->SetMode(gGraphicsOutput, PrefferedMode->ModeNumber);
    if (EFI_ERROR(Status)) {
        goto exit;
    }

    // Disable console output now that video mode is enabled
    ConsoleDisable();

    // Update the VBoot structure to the new mode
    VBoot->Video.Width = PrefferedMode->Width;
    VBoot->Video.Height = PrefferedMode->Height;
    VBoot->Video.BitsPerPixel = PrefferedMode->BitsPerPixel;
    VBoot->Video.FrameBuffer = gGraphicsOutput->Mode->FrameBufferBase;
    VBoot->Video.Pitch = PrefferedMode->BytesPerScanline;
    VBoot->Video.RedMask = PrefferedMode->Red.Mask;
    VBoot->Video.GreenMask = PrefferedMode->Green.Mask;
    VBoot->Video.BlueMask = PrefferedMode->Blue.Mask;
    VBoot->Video.ReservedMask = PrefferedMode->Reserved.Mask;
    VBoot->Video.RedPosition = PrefferedMode->Red.Position;
    VBoot->Video.GreenPosition = PrefferedMode->Green.Position;
    VBoot->Video.BluePosition = PrefferedMode->Blue.Position;
    VBoot->Video.ReservedPosition = PrefferedMode->Reserved.Position;

exit:
    LibraryFreeMemory(Modes);
    return Status;
}
