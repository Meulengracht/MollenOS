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
#include <os/sharedobject.h>
#include "test.hpp"

typedef const char*(*libpng_ver_func)(void *nullp);

class SharedObjectTests : public OSTest {
public:
    SharedObjectTests() : OSTest("SharedObjectTests") { }
    int RunTests() {
        int Errors = 0;
        TestLog(">> Testing SharedObjectLoad(libpng.dll)");
        Handle_t Handle = SharedObjectLoad("libpng.dll");
        if (Handle != HANDLE_INVALID) {
            TestLog(">> Loading symbol png_get_header_version");
            libpng_ver_func func = (libpng_ver_func)SharedObjectGetFunction(Handle, "png_get_header_version");
            if (func != NULL) {
                TestLog(func(NULL));
            }
            else {
                TestLog(">> failed to resolve function");
                Errors++;
            }
            if (SharedObjectUnload(Handle) != OsSuccess) {
                TestLog(">> failed to close library");
                Errors++;
            }
            else {
                TestLog(">> Library unloaded");
            }
        }
        else {
            TestLog(">> Failed to load library");
            Errors++;
        }
        TestLog(">> Testing SharedObjectLoad(NULL)");
        Handle = SharedObjectLoad(NULL);
        if (Handle != HANDLE_INVALID) {
            TestLog(">> We succeeded in resolving main image handle");
            if (SharedObjectUnload(Handle) != OsSuccess) {
                TestLog(">> failed to close handle");
                Errors++;
            }
            else {
                TestLog(">> Handle unloaded");
            }
        }
        else {
            TestLog(">> Failed to load main image handle");
            Errors++;
        }
        return Errors;
    }
};
