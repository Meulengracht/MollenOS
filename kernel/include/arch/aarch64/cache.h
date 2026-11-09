/**
 * Copyright, Philip Meulengracht
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

#ifndef __VALI_AARCH64_CACHE_H__
#define __VALI_AARCH64_CACHE_H__

#include <os/osdefs.h>

/**
 * @brief Cleans and invalidates all data and unified caches of this CPU, and
 * its instruction cache.
 *
 * Must run at EL1, after the loader has validated the CPU features at firmware
 * entry. This does not stop other CPUs or devices, so during a system-wide
 * power transition the caller must make sure nothing else can change the
 * memory involved. Asynchronous exceptions are masked on this CPU during the
 * walk, and the previous mask and CSSELR_EL1 are restored afterwards. The
 * stack and other data memory are never touched.
 *
 * Translation and caches stay enabled, so the caches start filling up again as
 * soon as execution continues. This only does the cache maintenance, and
 * anything else a suspend or resume needs is up to the caller.
 */
__EXTERN void
Arm64FlushCpuCaches(void);

#endif //!__VALI_AARCH64_CACHE_H__
