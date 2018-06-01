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
#pragma once
#include "../engine/elements/window.hpp"
#include "event.hpp"

class CWindowCreatedEvent : public CVioarrEvent {
public:
    CWindowCreatedEvent(CWindow *Window) : CVioarrEvent(EventWindowCreated) {
        m_Window = Window;
    }
    ~CWindowCreatedEvent() { }
    CWindow *GetWindow() const { return m_Window; }
private:
    CWindow *m_Window;
};

class CWindowUpdateEvent : public CVioarrEvent {
public:
    CWindowUpdateEvent(CWindow *Window) : CVioarrEvent(EventWindowUpdate) {
        m_Window = Window;
    }
    ~CWindowUpdateEvent() { }
    CWindow *GetWindow() const { return m_Window; }
private:
    CWindow *m_Window;
};

class CWindowDestroyEvent : public CVioarrEvent {
public:
    CWindowDestroyEvent(CWindow *Window) : CVioarrEvent(EventWindowDestroy) {
        m_Window = Window;
    }
    ~CWindowDestroyEvent() { }
    CWindow *GetWindow() const { return m_Window; }
private:
    CWindow *m_Window;
};
