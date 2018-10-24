/* MollenOS
 *
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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Alias & Process Management
 * - The implementation of phoenix is responsible for managing alias's, handle
 *   file events and creating/destroying processes.
 */

#ifndef __VALI_PROCESS_PIPES_H__
#define __VALI_PROCESS_PIPES_H__

#include <os/osdefs.h>

/* PhoenixOpenAshPipe
 * Creates a new communication pipe available for use. */
KERNELAPI OsStatus_t KERNELABI
PhoenixOpenAshPipe(
    _In_ MCoreAsh_t*    Ash, 
    _In_ int            Port, 
    _In_ int            Type);

/* PhoenixWaitAshPipe
 * Waits for a pipe to be opened on the given
 * ash instance. */
KERNELAPI OsStatus_t KERNELABI
PhoenixWaitAshPipe(
    _In_ MCoreAsh_t *Ash, 
    _In_ int         Port);

/* PhoenixCloseAshPipe
 * Closes the pipe for the given Ash, and cleansup
 * resources allocated by the pipe. This shutsdown
 * any communication on the port */
KERNELAPI OsStatus_t KERNELABI
PhoenixCloseAshPipe(
    _In_ MCoreAsh_t *Ash, 
    _In_ int         Port);

/* PhoenixGetAshPipe
 * Retrieves an existing pipe instance for the given ash
 * and port-id. If it doesn't exist, returns NULL. */
KERNELAPI SystemPipe_t* KERNELABI
PhoenixGetAshPipe(
    _In_ MCoreAsh_t     *Ash, 
    _In_ int             Port);

#endif // !__VALI_PROCESS_PIPES_H__
