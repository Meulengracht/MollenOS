/**
 * MollenOS
 *
 * Copyright 2019, Philip Meulengracht
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
 * WM Protocol Test
 *  - Spawns a minimal implementation of a wm server to test libwm and the
 *    communication protocols in the os
 */

#ifndef __PROTOCOL_TEST_H__
#define __PROTOCOL_TEST_H__

#define PROTOCOL_TEST_ID             0x0D
#define PROTOCOL_TEST_FUNCTION_COUNT 1

#define PROTOCOL_TEST_PRINT_ID 0x0

struct test_print_arg {
    char message[128];
};

struct test_print_ret {
    int status;
};

#endif //!__PROTOCOL_TEST_H__
