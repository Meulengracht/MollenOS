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
 * MollenOS - Vioarr Window Compositor System
 *  - The window compositor system and general window manager for
 *    MollenOS.
 */

#include <os/process.h>
#include "vioarr.hpp"
#include "engine/scene.hpp"
#include "engine/veightengine.hpp"
#include "utils/log_manager.hpp"

#if defined(_VIOARR_OSMESA)
#include "graphics/opengl/osmesa/display_osmesa.hpp"
#define DISPLAY_TYPE() CDisplayOsMesa()
#else
#include "graphics/soft/display_framebuffer.hpp"
#define DISPLAY_TYPE() CDisplayFramebuffer()
#endif

// Run
// The main program loop
int VioarrCompositor::Run()
{
    //std::chrono::time_point<std::chrono::steady_clock> LastUpdate;
    m_IsRunning = true;

    // Create the display
    sLog.Info("Creating display");
    m_Display = new DISPLAY_TYPE();
    if (!m_Display->Initialize()) {
        delete m_Display;
        return -2;
    }

    // Initialize V8 Engine
    sLog.Info("Initializing V8");
    sEngine.Initialize(m_Display);
    sEngine.AddScene(CreateDesktopScene());

    // Spawn handlers
    sLog.Info("Spawning message handler");
    SpawnInputHandlers();
    sLog.Disable();

    // Enter event loop
    sEngine.Render();
    while (m_IsRunning) {
        m_Signal.Wait();

        // LastUpdate = std::chrono::steady_clock::now();
        sEngine.Render();
        // auto render_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - LastUpdate); /*  milliseconds.count() */
        // auto fps = 1000.0f / render_time_ms;
    }
    return 0;
}

void VioarrCompositor::UpdateNotify()
{
    m_Signal.Signal();
}
