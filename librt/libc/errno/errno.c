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
 * Errno Support Definitions & Structures
 * - This file describes the base errno-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include "../threads/tls.h"
#include <errno.h>

#ifdef LIBC_KERNEL
errno_t __errno_global = 0;
errno_t *__errno(void) {
	return &__errno_global;
}
#else
errno_t *__errno(void) {
	return &((tls_current())->err_no);
}
#endif
