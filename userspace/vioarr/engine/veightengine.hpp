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
 * MollenOS - Vioarr Engine System (V8)
 *  - The Vioarr V8 Graphics Engine.
 */
#pragma once

/* Includes
 * - System */
#include "graphics/opengl/opengl_exts.hpp"
#include "graphics/display.hpp"

class CVEightEngine {
public:
	static CVEightEngine& GetInstance() {
		// Guaranteed to be destroyed.
		// Is instantiated on first use
		static CVEightEngine _Instance;
		return _Instance;
	}
private:
	CVEightEngine();
    ~CVEightEngine();

public:
	CVEightEngine(CVEightEngine const&) = delete;
	void operator=(CVEightEngine const&) = delete;

    void Initialize(CDisplay *Screen);
    void Render();

private:
    CDisplay*   m_Screen;
};

// Shorthand for the vioarr
#define sEngine CVEightEngine::GetInstance()
