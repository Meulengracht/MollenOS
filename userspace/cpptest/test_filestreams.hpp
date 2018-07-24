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
#include <fstream>
#include "test.hpp"

class FileStreamTests : public OSTest {
public:
    FileStreamTests() : OSTest("FileStreamTests") { }
    int RunTests() {
        int Errors = 0;
        std::ofstream fs_out;
        std::ifstream fs_in;
        std::string line;
        TestLog(">> creating file example.txt");
        fs_out.open("example.txt");
        if (fs_out.is_open()) {
            TestLog(">> testing writing text to a file");
            fs_out << "Writing this to a file.\n";
            TestLog(">> closing");
            fs_out.close();
        }
        else {
            TestLog(">> failed to open file for writing");
            Errors++;
        }
        TestLog(">> Testing reading text back from same file");
        fs_in.open("example.txt");
        if (fs_in.is_open()) {
            while (getline(fs_in, line) ) {
                TestLog(line);
            }
            fs_in.close();
        }
        else {
            TestLog(">> Failed to open file for reading");
            Errors++;
        }
        return Errors;
    }
};
