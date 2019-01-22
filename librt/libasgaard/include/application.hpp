/* ValiOS
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
 * ValiOS - Application Framework (Asgard)
 *  - Contains the implementation of the application framework used for building
 *    graphical applications.
 */
#pragma once

#include <ddk/input.h>
#include <string>
#include <thread>

namespace Asgard {
    class Rectangle;
    class EventQueue;

    class Application {
    public:
        Application(const Rectangle& SurfaceArea, const std::string& Name);
        ~Application();

        int Execute();

    protected:
        virtual int  Entry();
        virtual void WindowEvent();
        virtual void KeyEvent(SystemKey_t* Key);

    protected:
        void Invalidate();

    private:
        void WindowEventWorker();
        void KeyEventWorker();

    private:
        EventQueue*  m_EventQueue;
        bool         m_Invalidated;
        bool         m_Running;
        std::thread* m_WmEventThread;
        std::thread* m_KeyEventThread;
    };
}
