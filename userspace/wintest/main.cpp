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
 * MollenOS - Windowing Test Suite for Userspace
 *  - Runs a variety of userspace tests against the window manager to verify
 *    the stability and integrity of the operating system.
 */

#include <os/ui.h>
#include <chrono>
#include <thread>

void BufferFill(const char *Buffer, uint32_t Color, size_t Size) {
    uint32_t *Pointer       = (uint32_t*)Buffer;
    size_t NumOfIterations  = Size / 4;
    while (NumOfIterations) {
        *(Pointer++) = Color;
        NumOfIterations--;
    }
}

/*******************************************
 * Windowing Tests
 *******************************************/
void BasicWindowingTests() {
    UIWindowParameters_t WindowParameters   = { { { 0 } } };
    BufferObject_t *WindowBuffer            = NULL;
    UiParametersSetDefault(&WindowParameters);

    // Register the window as initial step
    UiRegisterWindow(&WindowParameters, &WindowBuffer);
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // Perform a window fill of color R
    BufferFill((const char*)GetBufferData(WindowBuffer), 
        0xFFFF0000, GetBufferSize(WindowBuffer));
    UiSwapBackbuffer();
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // Perform a window fill of color G
    BufferFill((const char*)GetBufferData(WindowBuffer), 
        0xFF00FF00, GetBufferSize(WindowBuffer));
    UiSwapBackbuffer();
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // Perform a window fill of color B
    BufferFill((const char*)GetBufferData(WindowBuffer), 
        0xFF0000FF, GetBufferSize(WindowBuffer));
    UiSwapBackbuffer();
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // Destroy window and cleanup
    UiUnregisterWindow();
}

/*******************************************
 * Entry Point
 *******************************************/
int main(int argc, char **argv) {
    BasicWindowingTests();
    return 0;
}
