/* MollenOS
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
 * MollenOS C11-Support Threading Implementation
 * - Definitions, prototypes and information needed.
 */

#ifndef __STDC_TLS__
#define __STDC_TLS__

#include <threads.h>

_CODE_BEGIN

/**
 * @brief Performs regular TSS cleanup. This is only needed to do on a process-level,
 * not once per thread.
 *
 * @param[In] threadID The thread if of the caller
 */
CRTDECL(void, tss_cleanup(thrd_t threadID));

_CODE_END

#endif //!__STDC_TLS__
