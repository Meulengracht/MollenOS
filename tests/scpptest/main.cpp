/* MollenOS
 *
 * Copyright 2018, Philip Meulengracht
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
#include <cstdlib>
#include <thread>

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
#include <map>
#include <iostream>

struct _TestObject { std::string a, b ; double d ; };
std::map<std::string, std::map< std::string, _TestObject >> _TestMap = {
    {
        "abc",
        {
            { "defg", { "one", "two", 1.2 } },
            { "hijk", { "three", "four", 3.4 } },
            { "lmno", { "five", "six", 5.6 } }
        }
    },
    
    {
        "pqr",
        {
            { "stuv", { "seven", "eight", 7.8 } },
            { "wxyz", { "nine", "zero", 9.0 } }
        }
    }
};

int TestGlobalInitialization() {
    for (const auto& mpair : _TestMap)
    {
        std::cout << mpair.first << '\n' ;
        for( const auto& cpair : mpair.second )
        {
            std::cout << "     " << cpair.first << '{' << cpair.second.a << ',' 
                      << cpair.second.b << ',' << cpair.second.d << "}\n" ;    
        }
    }
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
    return ErrorCounter;
}
