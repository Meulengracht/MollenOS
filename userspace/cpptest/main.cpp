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
 * C/C++ Test Suite for Userspace
 *  - Runs a variety of userspace tests against the libc/libc++ to verify
 *    the stability and integrity of the operating system.
 */

#include <ddk/utils.h>
#include "test.hpp"
#include "test_constreams.hpp"
#include "test_filestreams.hpp"
#include "test_processes.hpp"
#include "test_so.hpp"
#include <cstdlib>
#include <thread>
#include <png.h>

extern "C" int libm_main (int argc, char **argv);

class TestTLS {
public:
    TestTLS() {
        WARNING("TestTLS()");
        Value = 31;
    }

    void SetValue(int Value) {
        this->Value = Value;
    }

    int GetValue() {
        return Value;
    }

private:
    int Value;
};

/*******************************************
 * Tls Testing
 *******************************************/
thread_local TestTLS TlsObject;

void thread_function() {
    printf("2: Initial value of variable: %u\n", TlsObject.GetValue());
    TlsObject.SetValue(100);
    printf("2: New value of variable: %u\n", TlsObject.GetValue());
}

int TestThreading() {
    int ErrorCounter = 0;
    printf("1: Initial value of variable: %u \n", TlsObject.GetValue());
    std::thread tlsthread(thread_function);
    tlsthread.join();
    printf("1: New value of variable: %u\n", TlsObject.GetValue());
    if (TlsObject.GetValue() != 31) {
        ErrorCounter++;
    }
    return ErrorCounter;
}

void RunMeAtExit()
{
    printf("I was run at exit!\n");
}

/*******************************************
 * Global init test
 *******************************************/
#include "lib.hpp"
static CTestLib _TestLibInstance;
int TestGlobalInitialization() {
    printf("(M) Value of the global static: %i\n", _TestLibInstance.callme());
    if (_TestLibInstance.callme() != 42) return 1;
    return 0;
}

/*******************************************
 * Entry Point
 *******************************************/
int main(int argc, char **argv) {
    int ErrorCounter = 0;

    // Run tests that must be in source files
    atexit(RunMeAtExit);
    ErrorCounter += TestThreading();
    ErrorCounter += TestGlobalInitialization();
   
    // Run tests
    RUN_TEST_SUITE(ErrorCounter, ConsoleStreamTests);
    RUN_TEST_SUITE(ErrorCounter, SharedObjectTests);
    RUN_TEST_SUITE(ErrorCounter, FileStreamTests);
    RUN_TEST_SUITE(ErrorCounter, ProcessTests);

    // Run libm test
    //libm_main(argc, argv);
    return ErrorCounter;
}
