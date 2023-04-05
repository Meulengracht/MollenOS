/**
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * USB Controller Scheduler
 * - Contains the implementation of a shared controller scheduker
 *   for all the usb drivers
 */

//#define __TRACE
#define ALIGN(Val, Alignment, Roundup) ((Val & (Alignment-1)) > 0 ? (Roundup == 1 ? ((Val + Alignment) & ~(Alignment-1)) : Val & ~(Alignment-1)) : Val)

#include <assert.h>
#include <os/mollenos.h>
#include <usb/usb.h>
#include <ddk/utils.h>
#include "scheduler.h"
#include <stdlib.h>
#include <string.h>

void
UsbSchedulerSettingsCreate(
    _In_ UsbSchedulerSettings_t*    Settings,
    _In_ size_t                     FrameCount,
    _In_ size_t                     SubframeCount,
    _In_ size_t                     MaxBandwidthPerFrame,
    _In_ unsigned int                    Flags)
{
    assert(Settings != NULL);
    memset((void*)Settings, 0, sizeof(UsbSchedulerSettings_t));

    Settings->Flags                 = Flags;
    Settings->FrameCount            = FrameCount;
    Settings->SubframeCount         = SubframeCount;
    Settings->MaxBandwidthPerFrame  = MaxBandwidthPerFrame;
}

void
UsbSchedulerSettingsConfigureFrameList(
    _In_ UsbSchedulerSettings_t*    Settings,
    _In_ reg32_t*                   FrameList,
    _In_ uintptr_t                  FrameListPhysical)
{
    assert((Settings->Flags & USB_SCHEDULER_FRAMELIST) == 0);

    Settings->FrameList         = FrameList;
    Settings->FrameListPhysical = FrameListPhysical;
}

void
UsbSchedulerSettingsAddPool(
    _In_ UsbSchedulerSettings_t*    Settings,
    _In_ size_t                     ElementSize,
    _In_ size_t                     ElementAlignment,
    _In_ size_t                     ElementCount,
    _In_ size_t                     ElementCountReserved,
    _In_ size_t                     LinkBreathMemberOffset,
    _In_ size_t                     LinkDepthMemberOffset,
    _In_ size_t                     ObjectMemberOffset)
{
    UsbSchedulerPool_t* Pool;

    // Sanitize that we don't create more pools than maximum
    assert(Settings->PoolCount < USB_POOL_MAXCOUNT);
    Pool = &Settings->Pools[Settings->PoolCount++];

    Pool->ElementBaseSize           = ElementSize;
    Pool->ElementAlignedSize        = ALIGN(ElementSize, ElementAlignment, 1);
    Pool->ElementCount              = ElementCount;
    Pool->ElementCountReserved      = ElementCountReserved;
    Pool->ElementLinkBreathOffset = LinkBreathMemberOffset;
    Pool->ElementLinkDepthOffset  = LinkDepthMemberOffset;
    Pool->ElementObjectOffset     = ObjectMemberOffset;
}
