/**
 * MollenOS
 *
 * Copyright (C) Philip Meulengracht
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

#include <bus/pci/function.h>
#include <bus/rp1/rp1.h>

static const struct PciFunctionHandler* const g_functionHandlers[] = {
    &g_rp1PciHandler
};

const struct PciFunctionHandler*
PciFunctionHandlerFind(
    _In_ const struct PciDevice* device)
{
    for (size_t index = 0; index < SIZEOF_ARRAY(g_functionHandlers); index++) {
        if (g_functionHandlers[index]->Match(device)) {
            return g_functionHandlers[index];
        }
    }
    return NULL;
}
