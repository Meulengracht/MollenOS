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

#include <os/input.h>
#include <list>
#include "veightengine.hpp"
#include "backend/nanovg.h"

class CEntity {
public:
    CEntity(CEntity *Parent, NVGcontext* VgContext, const glm::vec3 &Position) {
        m_Parent        = Parent;
        if (m_Parent != nullptr) {
            m_Parent->AddEntity(this);
        }

        m_VgContext     = VgContext;
        m_vPosition     = Position;
    }
    CEntity(CEntity *Parent, NVGcontext* VgContext)
        : CEntity(Parent, VgContext, glm::vec3(0.0f, 0.0f, 0.0f)) { }
    CEntity(NVGcontext* VgContext) 
        : CEntity(nullptr, VgContext) { }
    virtual ~CEntity() { m_Children.remove_if([](CEntity* Element) { delete Element; return true; });  }

    void            AddEntity(CEntity *Entity) {
        auto Position = std::find(m_Children.begin(), m_Children.end(), Entity);
        if (Position == m_Children.end()) {
            m_Children.push_back(Entity);
            m_Children.sort([](const CEntity *lh, const CEntity *rh) { return lh->GetZ() < rh->GetZ(); }); // true == first first, false == second first
        }
    }
    void            RemoveEntity(CEntity *Entity) {
        auto Position = std::find(m_Children.begin(), m_Children.end(), Entity);
        if (Position != m_Children.end()) {
            m_Children.erase(Position);
        }
    }
    void            Render(NVGcontext* VgContext) {
        nvgSave(VgContext);
        nvgTranslate(VgContext, m_vPosition.x, m_vPosition.y);
        Draw(VgContext);
        for (auto itr = m_Children.begin(); itr != m_Children.end(); itr++) {
            (*itr)->Render(VgContext);
        }
        nvgRestore(VgContext);
    }

    void            PreProcess(size_t MilliSeconds) {
        Update(MilliSeconds);
        for (auto itr = m_Children.begin(); itr != m_Children.end(); itr++) {
            (*itr)->PreProcess(MilliSeconds);
        }
    }

    // Positioning
    void            Move(float X, float Y, float Z) {
        m_vPosition.x += X;
        m_vPosition.y += Y;
        m_vPosition.z += Z;
    }
    void            SetPosition(float X, float Y, float Z) {
        m_vPosition.x = X;
        m_vPosition.y = Y;
        m_vPosition.z = Z;
    }

    // Input handling
    virtual void    HandleKeyEvent(SystemKey_t* Key) { }
    
    // Setters
    void            SetX(float X)           { m_vPosition.x = X; }
    void            SetY(float Y)           { m_vPosition.y = Y; }
    void            SetZ(float Z)           { m_vPosition.y = Z; }

    // Getters
    const glm::vec3 &GetPosition() const    { return m_vPosition; }
    const float     GetX() const            { return m_vPosition.x; }
    const float     GetY() const            { return m_vPosition.y; }
    const float     GetZ() const            { return m_vPosition.z; }
    const std::list<CEntity*> &GetChildren() const { return m_Children; }

protected:
    // Overrideable methods
    virtual void    Update(size_t MilliSeconds) { }
    virtual void    Draw(NVGcontext* VgContext) { }

    CEntity*            m_Parent;
    NVGcontext*         m_VgContext;
    glm::vec3           m_vPosition;
    std::list<CEntity*> m_Children;
};
