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

#ifndef __ETHERNET_DIAGNOSTICS_H__
#define __ETHERNET_DIAGNOSTICS_H__

// Optional initialization and packet diagnostics. Keep packet logs bounded at
// the call site. Errors needed to diagnose a failed device use ERROR or WARNING
// directly so disabling these diagnostics does not hide them.
#ifdef VALI_ETHERNET_DIAGNOSTICS
#include <ddk/utils.h>
#define ETHERNET_LOG(...) NOTICE(__VA_ARGS__)
#else
// Arguments are not evaluated when diagnostics are disabled.
#define ETHERNET_LOG(...) ((void)0)
#endif

#endif
