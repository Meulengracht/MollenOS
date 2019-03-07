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
#include <os/process.h>
#include <iostream>
#include <fstream>
#include <cstdio>
#include <thread>
#include <io.h>
#include "test.hpp"

class ProcessTests : public OSTest {
public:
    ProcessTests() : OSTest("ProcessTests") { }

    int TestSpawnProcess(const std::string& Path)
    {
        TestLog("TestSpawnProcess(%s)", Path.c_str());

        ProcessStartupInformation_t StartupInformation;
        InitializeStartupInformation(&StartupInformation);
    
        // Set inheritation
        TestLog(">> process stdout handle = %i", m_Stdout);
        StartupInformation.InheritFlags = PROCESS_INHERIT_STDOUT;
        StartupInformation.StdOutHandle = m_Stdout;

        UUId_t ProcessId = ProcessSpawnEx(Path.c_str(), nullptr, &StartupInformation);
        TestLog(">> process id = %u", ProcessId);
        if (ProcessId != UUID_INVALID) {
            int ExitCode = 0;
            ProcessJoin(ProcessId, 0, &ExitCode);
            TestLog(">> process exitted with code = %i", ExitCode);
        }
        else {
            TestLog(">> failed to spawn process");
            return 1;
        }
        return 0;
    }

    int TestSpawnInvalidProcess()
    {
        TestLog("TestSpawnInvalidProcess");
        UUId_t ProcessId = ProcessSpawn("plx don't exist", nullptr);
        TestLog(">> process id = %u", ProcessId);
        if (ProcessId == UUID_INVALID) {
            TestLog(">> process failed successfully");
        }
        else {
            TestLog(">> process failed to fail");
            return 1;
        }
        return 0;
    }

    void StdoutListener()
    {
        char ReadBuffer[256];
        if (m_Stdout == -1) {
            TestLog("FAILED TO CREATE STDOUT\n");
            return;
        }

        while (true) {
            std::memset(&ReadBuffer[0], 0, sizeof(ReadBuffer));
            read(m_Stdout, &ReadBuffer[0], sizeof(ReadBuffer));
            TestLog("stdout: %s", &ReadBuffer[0]);
        }
    }

    int RunTests() {
        int Errors  = 0;
        m_Stdout    = pipe();
        std::thread stdout_listener(&ProcessTests::StdoutListener, this);
        stdout_listener.detach();

        Errors += TestSpawnProcess("macia.app");
        Errors += TestSpawnInvalidProcess();
        return Errors;
    }

private:
    int m_Stdout;
};
