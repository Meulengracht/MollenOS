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
 * MollenOS - Vioarr Engine System (V8)
 *  - The Vioarr V8 Graphics Engine.
 */
#pragma once

#include <list>
#include "entity.hpp"

class CScene {
public:
    CScene(CEntity* RootLayer) : m_RootLayer(RootLayer), m_TopmostEntity(nullptr) {}
    ~CScene() {
        m_WindowLayer.remove_if([](CEntity* Element) { delete Element; return true; });
        m_PriorityLayer.remove_if([](CEntity* Element) { delete Element; return true; });
        if (m_RootLayer != nullptr) {
            delete m_RootLayer;
        }
    }

    void                AddWindow(CEntity* Window) {
        auto Position = std::find(m_WindowLayer.begin(), m_WindowLayer.end(), Window);
        if (Position == m_WindowLayer.end()) {
            m_WindowLayer.push_back(Window);
            m_WindowLayer.sort([](const CEntity *lh, const CEntity *rh) { return lh->GetZ() < rh->GetZ(); }); // true == first first, false == second first
        }
    }

    bool                RemoveWindow(CEntity* Window) {
        auto Position = std::find(m_WindowLayer.begin(), m_WindowLayer.end(), Window);
        if (Position != m_WindowLayer.end()) {
            m_WindowLayer.erase(Position);
            delete Window;
            return true;
        }
        return false;
    }

    void                AddPriority(CEntity* Prioity) {
        auto Position = std::find(m_PriorityLayer.begin(), m_PriorityLayer.end(), Priority);
        if (Position == m_PriorityLayer.end()) {
            m_PriorityLayer.push_back(Priority);
            m_PriorityLayer.sort([](const CEntity *lh, const CEntity *rh) { return lh->GetZ() < rh->GetZ(); }); // true == first first, false == second first
        }
    }

    bool                RemovePriority(CEntity* Priority) {
        auto Position = std::find(m_PriorityLayer.begin(), m_PriorityLayer.end(), Priority);
        if (Position != m_PriorityLayer.end()) {
            m_PriorityLayer.erase(Position);
            delete Priority;
            return true;
        }
        return false;
    }

    void                ProxyKeyEvent(SystemKey_t* Key) {
        if (m_TopmostEntity != nullptr) {
            m_TopmostEntity->HandleKeyEvent(Key);
        }
    }

    void                Update(size_t MilliSeconds) {
        if (m_RootLayer != nullptr) {
            m_RootLayer->PreProcess(MilliSeconds);
        }
        for (auto e : m_WindowLayer) { e->PreProcess(MilliSeconds); }
        for (auto e : m_PriorityLayer) { e->PreProcess(MilliSeconds); }
    }

    void                Render(NVGcontext* VgContext) {
        if (m_RootLayer != nullptr) {
            m_RootLayer->Render(VgContext);
        }
        for (auto e : m_WindowLayer) { e->Render(VgContext); }
        for (auto e : m_PriorityLayer) { e->Render(VgContext); }
    }

private:
    CEntity*            m_RootLayer;
    std::list<CEntity*> m_WindowLayer;
    std::list<CEntity*> m_PriorityLayer;
    CEntity*            m_TopmostEntity;
};
