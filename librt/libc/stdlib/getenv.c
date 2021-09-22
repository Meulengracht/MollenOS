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

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// should be located in tls data and instantiated from session
static char* g_nullEnvironment[] = {
        NULL
};

// A pointer to the current environment
static char** g_currentEnvironment = &g_nullEnvironment[0];

static char*** get_tls_environment()
{
	// @todo
	return &g_currentEnvironment;
}

char* getenv(const char* name)
{
	char***         environment = get_tls_environment();
	register int    length;
	register char** p;
	const char*     c;

	// sanitize that we have an environment installed currently
	if (!*environment) {
		return NULL;
	}

	// skip to end of name, making sure that name does not contain an '='
	c = name;
	while (*c && *c != '=') {
		 c++;
	}

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

int setenv(const char* name, const char* value, int override)
{
	char*** environment = get_tls_environment();

	// sanitize that we have an environment installed currently
	if (!*environment) {
		_set_errno(ENOTSUP);
		return -1;
	}

	// @todo
	return 0;
}

int unsetenv(const char* name)
{
	char*** environment = get_tls_environment();

	// sanitize that we have an environment installed currently
	if (!*environment) {
		_set_errno(ENOTSUP);
		return -1;
	}

	// @todo
	return 0;
}
