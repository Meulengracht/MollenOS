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

#if defined(__aarch64__)
#include <platform/arm64.h>
#elif defined(__amd64__) || defined(__i386__)
#include <platform/x86.h>
#endif

#include <console.h>
#include <video.h>

#include <Protocol/GraphicsOutput.h>

#include <Library/BaseMemoryLib.h>

static EFI_GUID gGraphicsOutputProtocolGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;

static const struct __PreferredMode {
    UINT32 Width;
    UINT32 Height;
} gPreferredModes[] = {
    { 1280, 1024 },
    { 1024, 768  },
    { 640,  480  },
    { 800,  600  }
};

static BOOLEAN
__ConvertColorMask(
    IN  UINT32  Value,
    OUT UINT32* Position,
    OUT UINT32* Mask)
{
    *Position = 0;
    *Mask = 0;
    
    if (!Value) {
        return TRUE;
    }

    while (!(Value & 1)) {
        ++*Position;
        Value >>= 1;
    }

    *Mask = Value;

    // Shift the channel mask to bit zero, then require contiguous bits.
    return (Value & (Value + 1U)) == 0;
}

static BOOLEAN
__ParseVideoFormat(
    IN  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* Info,
    OUT struct VBootVideo*                    Video)
{
    EFI_PIXEL_BITMASK pixel;
    UINT32            bits;
    UINT32            bytes;
    UINT32            mask;

    SetMem(Video, sizeof(*Video), 0);
    if (!Info->HorizontalResolution || !Info->VerticalResolution ||
        Info->PixelsPerScanLine < Info->HorizontalResolution) {
        return FALSE;
    }

    switch (Info->PixelFormat) {
        case PixelRedGreenBlueReserved8BitPerColor:
            pixel = (EFI_PIXEL_BITMASK){0xff, 0xff00, 0xff0000, 0xff000000};
            break;
        case PixelBlueGreenRedReserved8BitPerColor:
            pixel = (EFI_PIXEL_BITMASK){0xff0000, 0xff00, 0xff, 0xff000000};
            break;
        case PixelBitMask:
            pixel = Info->PixelInformation;
            break;
        default:
            // BLT-only modes have no framebuffer for the kernel to use.
            return FALSE;
    }

    if (!pixel.RedMask || !pixel.GreenMask || !pixel.BlueMask) {
        return FALSE;
    }

    if ((pixel.RedMask & pixel.GreenMask) || (pixel.RedMask & pixel.BlueMask) ||
        (pixel.GreenMask & pixel.BlueMask)) {
        return FALSE;
    }

    mask = pixel.RedMask | pixel.GreenMask | pixel.BlueMask;
    if (mask & pixel.ReservedMask) {
        return FALSE;
    }

    if (!__ConvertColorMask(pixel.RedMask, &Video->RedPosition, &Video->RedMask) ||
        !__ConvertColorMask(pixel.GreenMask, &Video->GreenPosition, &Video->GreenMask)) {
        return FALSE;
    }

    if (!__ConvertColorMask(pixel.BlueMask, &Video->BluePosition, &Video->BlueMask) ||
        !__ConvertColorMask(pixel.ReservedMask, &Video->ReservedPosition, &Video->ReservedMask)) {
        return FALSE;
    }

    mask |= pixel.ReservedMask;
    bits = 0;
    while (mask) {
        ++bits;
        mask >>= 1;
    }
    
    // The boot handoff supports 32-bit modes only.
    bytes = (bits + 7) / 8;
    if (bytes != 4) {
        return FALSE;
    }

    if (Info->PixelsPerScanLine > MAX_UINT32 / bytes) {
        return FALSE;
    }

    Video->Width = Info->HorizontalResolution;
    Video->Height = Info->VerticalResolution;
    Video->BitsPerPixel = bytes * 8;
    Video->Pitch = Info->PixelsPerScanLine * bytes;
    return TRUE;
}

static BOOLEAN
__CurrentMode(
    IN  EFI_GRAPHICS_OUTPUT_PROTOCOL* Gop,
    OUT struct VBootVideo*            Video)
{
    UINT64 length;

    if (!Gop->Mode || !Gop->Mode->Info ||
        Gop->Mode->SizeOfInfo < sizeof(*Gop->Mode->Info)) {
        return FALSE;
    }

    if (!__ParseVideoFormat(Gop->Mode->Info, Video)) {
        return FALSE;
    }

    length = (UINT64)Video->Pitch * Video->Height;
    if (!Gop->Mode->FrameBufferBase || length > Gop->Mode->FrameBufferSize ||
        Gop->Mode->FrameBufferBase > MAX_UINT64 - length) {
        return FALSE;
    }

    Video->FrameBuffer = Gop->Mode->FrameBufferBase;
    return TRUE;
}

static BOOLEAN
__TryMode(
    IN  EFI_GRAPHICS_OUTPUT_PROTOCOL* Gop,
    IN  UINT32                        Number,
    IN  const struct __PreferredMode* Preference,
    OUT struct VBootVideo*            Video)
{
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* info = NULL;
    EFI_STATUS                            status;
    UINTN                                 size;
    BOOLEAN                               usable;

    status = Gop->QueryMode(Gop, Number, &size, &info);
    if (EFI_ERROR(status)) {
        return FALSE;
    }

    usable = info && size >= sizeof(*info);
    if (usable && Preference) {
        usable = info->HorizontalResolution == Preference->Width &&
            info->VerticalResolution == Preference->Height;
    }

    if (usable) {
        usable = __ParseVideoFormat(info, Video);
    }

    if (info) {
        LibraryFreeMemory(info);
    }

    if (!usable) {
        return FALSE;
    }

    if (Gop->Mode->Mode != Number) {
        status = Gop->SetMode(Gop, Number);
        if (EFI_ERROR(status)) {
            return FALSE;
        }
    }
    return __CurrentMode(Gop, Video);
}

EFI_STATUS
VideoInitialize(
    IN struct VBoot* VBoot)
{
    EFI_GRAPHICS_OUTPUT_PROTOCOL* gop;
    struct VBootVideo             video;
    EFI_STATUS                    status;
    UINTN                         preference;
    UINT32                        number;
    UINT32                        maxMode;

    SetMem(&VBoot->Video, sizeof(VBoot->Video), 0);
    status = gBS->LocateProtocol(&gGraphicsOutputProtocolGuid, NULL, (VOID**)&gop);
    if (EFI_ERROR(status) || !gop->Mode) {
        return EFI_SUCCESS;
    }

    maxMode = gop->Mode->MaxMode;
    for (preference = 0; preference < ARRAY_SIZE(gPreferredModes); preference++) {
        for (number = 0; number < maxMode; number++) {
            if (__TryMode(gop, number, &gPreferredModes[preference], &video)) {
                goto ready;
            }
        }
    }

    if (__CurrentMode(gop, &video)) {
        goto ready;
    }

    for (number = 0; number < maxMode; number++) {
        if (__TryMode(gop, number, NULL, &video)) {
            goto ready;
        }
    }

    return EFI_SUCCESS;

ready:
    VBoot->Video = video;
#if !defined(__aarch64__)
    ConsoleDisable();
#endif
    return EFI_SUCCESS;
}
