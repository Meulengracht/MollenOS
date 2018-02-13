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

/* Includes
 * - System */
#define __TRACE
#include <os/utils.h>
#include <string>

class CLogManager {
public:
	static CLogManager& GetInstance() {
		// Guaranteed to be destroyed.
		// Is instantiated on first use
		static CLogManager _Instance;
		return _Instance;
	}
private:
	CLogManager() {}                     // Constructor? (the {} brackets) are needed here.

public:
	CLogManager(CLogManager const&) = delete;
	void operator=(CLogManager const&) = delete;

    void Info(const std::string &Message) {
        TRACE(Message.c_str());
    }

    void Warning(const std::string &Message) {
        WARNING(Message.c_str());
    }

    void Error(const std::string &Message) {
        ERROR(Message.c_str());
    }
};

// Shorthand for the vioarr
#define sLog CLogManager::GetInstance()
