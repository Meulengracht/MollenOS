/* MollenOS
 *
 * Copyright 2011 - 2018, Philip Meulengracht
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
 * MollenOS
 */

#include <stdio.h>
#define TESTLIB
#include "lib.hpp"

CTestLib::CTestLib() {
    _val = 42;
}

int CTestLib::callme() {
    return _val;
}

// Global, static init
static CTestLib _TestLibInstance;

void dllmain(int action) {
    printf("dllmain(action = %i)\n", action);
    printf("(L) Value of the global static: %i\n", _TestLibInstance.callme());
}
