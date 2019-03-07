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

#include <os/mollenos.h>
#include <iostream>
#include <fstream>
#include <cstdio>
#include <io.h>
#include "test.hpp"

class FileStreamTests : public OSTest {
public:
    FileStreamTests() : OSTest("FileStreamTests") { }

    int TestFileCreation()
    {
        std::ofstream fs_out;
        TestLog(">> TestFileCreation()");

        fs_out.open("my_output.txt");
        if (fs_out.is_open()) {
            fs_out.close();
            return 0;
        }
        TestLog(">> failed to create file");
        return 1;
    }

    int TestFileWrite()
    {
        std::ofstream fs_out;
        TestLog(">> TestFileWrite()");

        fs_out.open("my_output.txt");
        if (fs_out.is_open()) {
            fs_out << "Writing this to a file.\n";
            fs_out.close();
            return 0;
        }
        TestLog(">> failed to open or create file");
        return 1;
    }

    int TestFileRead()
    {
        std::ifstream fs_in;
        std::string line;

        TestLog(">> TestFileRead()");
        fs_in.open("my_output.txt");
        if (fs_in.is_open()) {
            while (getline(fs_in, line) ) {
                TestLog(line.c_str());
            }
            fs_in.close();
            return 0;
        }
        TestLog(">> failed to open file");
        return 1;
    }

    int TestFileDeletion()
    {
        TestLog(">> TestFileDeletion()");
        if (remove("my_output.txt")) {
            TestLog(">> failed to remove file");
            return 1;
        }
        return 0;
    }

    int TestFileStats()
    {
        OsFileDescriptor_t  Stats;
        FileSystemCode_t    Status;

        TestLog(">> TestFileStats()");
        Status = GetFileInformationFromPath("macia.app", &Stats);
        if (Status != FsOk) {
            TestLog(" >> failed to stat macia.app");
            return 1;
        }
        TestLog(">> macia: %u - %u", Stats.Flags, Stats.Permissions);
        
        Status = GetFileInformationFromPath("maccccc.app", &Stats);
        if (Status == FsOk) {
            TestLog(" >> failed to fail on stat with macccc.app");
            return 1;
        }
        return 0;
    }

    int TestDirectoryIteration()
    {
        struct DIR* directory;
        struct DIRENT direntry;
        TestLog(">> TestDirectoryIteration()");

        if (opendir("$bin", O_RDONLY, &directory)) {
            TestLog(">> failed to open directory");
            return 1;
        }

        while (readdir(directory, &direntry) != -1) {
            TestLog(&direntry.d_name[0]);
        }

        if (closedir(directory)) {
            TestLog(">> failed to close directory");
            return 1;
        }
        return 0;
    }

    int RunTests() {
        int Errors = 0;

        // File tests
        Errors += TestFileCreation();
        Errors += TestFileWrite();
        Errors += TestFileRead();
        Errors += TestFileDeletion();
        Errors += TestFileStats();

        // Directory tests
        Errors += TestDirectoryIteration();
        // Directory creation
        // Directory deletion
        return Errors;
    }
};
