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

class CVioarrEventBase {
public:
    enum EEventType {
        EventUpdate,

        EventWindowCreated,
        EventWindowDestroy,

        EventPriorityCreated,
        EventPriorityDestroyed
    };

public:
    CVioarrEventBase(EEventType Type) : m_Type(Type) { }
    virtual ~CVioarrEventBase() { }

    template<class T>
    T*              GetData() { return static_cast<T*>(GetDataPointer())}
    EEventType      GetType() { return m_Type; }

protected:
    virtual void*   GetDataPointer() = 0;

private:
   const EEventType m_Type;
};

template<class T, typename... Args>
class CVioarrEvent : public CVioarrEventBase {
public:
    CVioarrEvent(EEventType type, Args... args) 
        : CVioarrEventBase(type), m_Data(args...) { }

private:
    void*   GetDataPointer() override { return &m_Data; }
    T       m_Data;
};
