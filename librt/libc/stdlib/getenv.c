/**
 * MollenOS
 *
 * Copyright 2016, Philip Meulengracht
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
 * MollenOS C Library - Get Environment Variable
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* A NULL environment, the day we support env
 * variables this should be in TLS and initialized from the session manager */
static char* NullEnvironment[] = {
        NULL
};

// A pointer to the current environment
char** CurrentEnvironment = &NullEnvironment[0];

char* getenv(const char *name)
{
	char***         environment = &CurrentEnvironment;
	register int    length;
	register char** p;
	const char*     c;

	/* Sanitize the env variable, the first entry
	 * may not be null actually */
	if (!*environment) {
		return NULL;
	}

	/* Set inital state */
	c = name;
	while (*c && *c != '=')  c++;

	/* Identifiers may not contain an '=', so cannot match if does */
	if (*c != '=') {
        length = c - name;
		for (p = *environment; *p; ++p) {
			if (!strncmp(*p, name, length)) {
				if (*(c = *p + length) == '=') {
					return (char *)(++c);
				}
			}
		}
	}
	return NULL;
}
