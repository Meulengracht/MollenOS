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
 * Operating System Statistics Application
 */

#include <stdio.h>

int main(int argc, char** argv)
{
    printf("mollenos vali operating system\n");
    printf("version: v" OS_VERSION "\n");
    printf("build: " OS_BUILD "-" OS_BUILD_DATE "\n");
    printf("arch: " OS_ARCH "\n");
    return 0;
}
