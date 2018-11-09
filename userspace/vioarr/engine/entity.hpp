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
#include <functional>
#include <list>
#include "backend/nanovg.h"
#include "veightengine.hpp"
#include "../vioarr.hpp"

class CEntity {
protected:
    CEntity(CEntity *Parent, NVGcontext* VgContext, const glm::vec3 &Position) {
        m_Parent = Parent;
        if (m_Parent != nullptr) {
            m_Parent->AddEntity(this);
        }

        m_VgContext = VgContext;
        m_vPosition = Position;
        m_Owner     = UUID_INVALID;
        m_Active    = false;
        m_Visible   = true;
        m_Id        = g_EntityId++;
    }
    CEntity(CEntity *Parent, NVGcontext* VgContext)
        : CEntity(Parent, VgContext, glm::vec3(0.0f, 0.0f, 0.0f)) { }
    CEntity(NVGcontext* VgContext) 
        : CEntity(nullptr, VgContext) { }
    CEntity(CEntity const&)      = default;
    CEntity& operator=(CEntity&) = default;

public:
    virtual ~CEntity() { m_Children.remove_if([](CEntity* Element) { delete Element; return true; });  }

public:
    void AddEntity(CEntity *Entity) {
        auto Position = std::find(m_Children.begin(), m_Children.end(), Entity);
        if (Position == m_Children.end()) {
            m_Children.push_back(Entity);
        }
    }

    void RemoveEntity(CEntity *Entity) {
        auto Position = std::find(m_Children.begin(), m_Children.end(), Entity);
        if (Position != m_Children.end()) {
            m_Children.erase(Position);
        }
    }

    void Invalidate()
    {
        Update();
        for (auto itr = m_Children.begin(); itr != m_Children.end(); itr++) {
            (*itr)->Invalidate();
        }
    }

    void Render(NVGcontext* VgContext) {
        if (!m_Visible) {
            return;
        }

        nvgSave(VgContext);
        nvgTranslate(VgContext, m_vPosition.x, m_vPosition.y);
        Draw(VgContext);
        for (auto itr = m_Children.begin(); itr != m_Children.end(); itr++) {
            (*itr)->Render(VgContext);
        }
        nvgRestore(VgContext);
    }

    // Positioning
    void Move(float X, float Y, float Z) {
        m_vPosition.x += X;
        m_vPosition.y += Y;
        m_vPosition.z += Z;
    }
    void SetPosition(float X, float Y, float Z) {
        m_vPosition.x = X;
        m_vPosition.y = Y;
        m_vPosition.z = Z;
    }

    // Input handling
    virtual void HandleKeyEvent(SystemKey_t* Key) { 
        for (auto itr = m_Children.begin(); itr != m_Children.end(); itr++) {
            if ((*itr)->IsActive()) {
                (*itr)->HandleKeyEvent(Key);
            }
        }
    }

    // State Changes
    void SetActive(bool Active)  {
        // Make sure we set all children inactive in that case
        if (!Active) {
            for (auto itr = m_Children.begin(); itr != m_Children.end(); itr++) {
                (*itr)->SetActive(Active);
            }
        }
        OnActivationChange(Active);
        m_Active = Active;
    }

    // Setters
    void SetX(float X)           { m_vPosition.x = X; }
    void SetY(float Y)           { m_vPosition.y = Y; }
    void SetZ(float Z)           { m_vPosition.y = Z; }
    void SetOwner(UUId_t Owner)  { m_Owner = Owner; }
    void SetVisible(bool Show)   { m_Visible = Show; }

    // Getters
    const glm::vec3&    GetPosition() const     { return m_vPosition; }
    float               GetX() const            { return m_vPosition.x; }
    float               GetY() const            { return m_vPosition.y; }
    float               GetZ() const            { return m_vPosition.z; }
    UUId_t              GetOwner() const        { return m_Owner; }
    bool                IsPriority() const      { return m_Priority; }
    bool                IsActive() const        { return m_Active; }
    bool                IsVisible() const       { return m_Visible; }
    long                GetId() const           { return m_Id; }

protected:
    // Overrideable methods
    virtual void Update()                        { }
    virtual void Draw(NVGcontext* VgContext)     { }
    virtual void OnActivationChange(bool Active) { }

    // Inheritabled methods
    void InvalidateScreen() {
        sVioarr.UpdateNotify();
    }
    
    long                m_Id;
    CEntity*            m_Parent;
    NVGcontext*         m_VgContext;
    glm::vec3           m_vPosition;
    std::list<CEntity*> m_Children;
    bool                m_Active;
    bool                m_Visible;
    UUId_t              m_Owner;
    bool                m_Priority;

    static long         g_EntityId;
};

class CPriorityEntity : public CEntity {
public:
    enum EPriorityBehaviour {
        DeleteOnFocusLost   = 0x1,
        DeleteOnEscape      = 0x2
    };

protected:
    CPriorityEntity(CEntity *Parent, NVGcontext* VgContext, const glm::vec3 &Position, unsigned int Behaviour)
        : CEntity(Parent, VgContext, Position), m_Behaviour(Behaviour) { m_Priority = true; }
    CPriorityEntity(CEntity *Parent, NVGcontext* VgContext, unsigned int Behaviour)
        : CPriorityEntity(Parent, VgContext, glm::vec3(0.0f, 0.0f, 0.0f), Behaviour) { }
    CPriorityEntity(NVGcontext* VgContext, unsigned int Behaviour) 
        : CPriorityEntity(nullptr, VgContext, Behaviour) { }
    CPriorityEntity(CPriorityEntity const&)         = default;
    CPriorityEntity& operator=(CPriorityEntity&)    = default;
    
public:
    virtual ~CPriorityEntity() = default;

    bool HasBehaviour(unsigned int Behaviour) const { return (m_Behaviour & Behaviour) != 0; }

private:
    unsigned int m_Behaviour;
};
