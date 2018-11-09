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
 * MollenOS - Vioarr Window Compositor System
 *  - The window compositor system and general window manager for
 *    MollenOS.
 */
#pragma once
#define __TRACE

#include <os/mollenos.h>
#include <os/utils.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>

class CLogManager {
public:
	static CLogManager& GetInstance() {
		// Guaranteed to be destroyed.
		// Is instantiated on first use
		static CLogManager _Instance;
		return _Instance;
	}
private:
	CLogManager() : m_Enabled(true) {}

public:
	CLogManager(CLogManager const&)     = delete;
	void operator=(CLogManager const&)  = delete;

    void Info(const char* Format, ...) {
        if (m_Enabled) {
            va_list Arguments;

            va_start(Arguments, Format);
            memset(&m_OutputBuffer[0], 0, sizeof(m_OutputBuffer));
            vsnprintf(&m_OutputBuffer[0], sizeof(m_OutputBuffer), Format, Arguments);
            va_end(Arguments);
            TRACE(&m_OutputBuffer[0]);
        }
    }

    void Warning(const char* Format, ...) {
        if (m_Enabled) {
            va_list Arguments;

            va_start(Arguments, Format);
            memset(&m_OutputBuffer[0], 0, sizeof(m_OutputBuffer));
            vsnprintf(&m_OutputBuffer[0], sizeof(m_OutputBuffer), Format, Arguments);
            va_end(Arguments);
            WARNING(&m_OutputBuffer[0]);
        }
    }

    void Error(const char* Format, ...) {
        if (m_Enabled) {
            va_list Arguments;

            va_start(Arguments, Format);
            memset(&m_OutputBuffer[0], 0, sizeof(m_OutputBuffer));
            vsnprintf(&m_OutputBuffer[0], sizeof(m_OutputBuffer), Format, Arguments);
            va_end(Arguments);
            ERROR(&m_OutputBuffer[0]);
        }
    }

    void Disable() {
        m_Enabled = false;
        MollenOSEndBoot();
    }

private:
    char m_OutputBuffer[256];
    bool m_Enabled;
};

// Shorthand for the vioarr
#define sLog CLogManager::GetInstance()
