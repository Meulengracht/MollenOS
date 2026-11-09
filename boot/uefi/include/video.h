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

#ifndef __VIDEO_H__
#define __VIDEO_H__

#include <Uefi.h>
#include <vboot/vboot.h>

/**
 * @brief Selects a usable GOP framebuffer when one is available.
 * @param VBoot Receives the framebuffer, or a zeroed descriptor when none is usable.
 * @return EFI_SUCCESS. Boot services must remain available.
 */
EFI_STATUS
VideoInitialize(
    IN struct VBoot* VBoot);

#endif //!__VIDEO_H__
