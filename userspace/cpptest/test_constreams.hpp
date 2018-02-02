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
 * MollenOS - C/C++ Test Suite for Userspace
 *  - Runs a variety of userspace tests against the libc/libc++ to verify
 *    the stability and integrity of the operating system.
 */
#pragma once
#include <iostream>
#include "test.hpp"

class ConsoleStreamTests : public OSTest {
public:
    ConsoleStreamTests() : OSTest("ConsoleStreamTests") { }
    int RunTests() {
        int Errors = 0;
        TestLog(">> Testing stdout, stderr");
        fprintf(stdout, "This is a message printed by stdout\n");
        fprintf(stderr, "This is a message printed by stderr\n");
        TestLog(">> Testing cout");
        std::cout << "This is a message printed by cout" << std::endl;
        TestLog(">> Testing complete");
        return Errors;
    }
};
