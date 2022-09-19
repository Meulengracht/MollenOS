/* MollenOS
 *
 * Copyright 2011 - 2016, Philip Meulengracht
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

#include <internal/_tls.h>
#include <stdlib.h>

/* Generate a new random value based on
 * the seed, the formula is taken from reactos */
int rand(void) {
	unsigned int seed = __tls_current()->seed;
    seed = seed * 214013L + 2531011L;
	__tls_current()->seed = seed;
	return (int)(seed >> 16) & RAND_MAX;
}

/* Set a new custom seed for this threads
 * random number generator */
void srand(unsigned int seed) {
    __tls_current()->seed = seed;
}
