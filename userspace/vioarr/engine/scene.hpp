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

#include <memory>
#include <list>
#include "entity.hpp"

class CScene {
public:
    CScene(CEntity* RootLayer) : m_TopmostEntity(nullptr) {
        m_RootLayer.reset(RootLayer);
    }
    ~CScene() {
        m_EntityLayer.clear();
        m_PriorityLayer.clear();
    }

    void                Add(CEntity* Entity) {
        m_EntityLayer.push_back(std::unique_ptr<CEntity>(Entity));
        SetActive(Entity);
    }

    bool                Remove(CEntity* Entity) {
        auto i = m_EntityLayer.begin();
        while (i != m_EntityLayer.end()) {
            if (i->get() == Entity) {
                m_EntityLayer.erase(i);
                UpdateNextActive(Entity);
                return true;
            }
            i++;
        }
        return false;
    }

    void                MoveToFront(CEntity* Entity) {
        auto i = m_EntityLayer.begin();
        while (i != m_EntityLayer.end()) {
            if (i->get() == Entity) {
                break;
            }
            i++;
        }
        m_EntityLayer.splice(m_EntityLayer.end(), m_EntityLayer, i);
        SetActive(Entity);
    }

    CEntity*            GetEntityWithOwner(UUId_t Owner) {
        for (auto& e : m_EntityLayer) {
            if (e->GetOwner() == Owner) {
                return e.get();
            }
        }
        return nullptr;
    }

    bool                HasEntity(CEntity* Entity) {
        for (auto& e : m_EntityLayer) {
            if (e.get() == Entity) {
                return true;
            }
        }
        return false;
    }

    void                AddPriority(CPriorityEntity* Priority) {
        // Remove all entities that die upon lost focus
        m_PriorityLayer.remove_if([](std::unique_ptr<CPriorityEntity> const& e) { return e->HasBehaviour(CPriorityEntity::DeleteOnFocusLost); });
        m_PriorityLayer.push_back(std::unique_ptr<CPriorityEntity>(Priority));
        SetPriorityActive(Priority);
    }

    bool                RemovePriority(CPriorityEntity* Priority) {
        auto i = m_PriorityLayer.begin();
        while (i != m_PriorityLayer.end()) {
            if (i->get() == Priority) {
                m_PriorityLayer.erase(i);
                UpdateNextActive(Priority);
                return true;
            }
            i++;
        }
        return false;
    }

    void                ProxyKeyEvent(SystemKey_t* Key) {
        if (m_TopmostEntity != nullptr) {
            if (Key->KeyCode == VK_ESCAPE && m_TopmostEntity->IsPriority()) {
                CPriorityEntity* Entity = static_cast<CPriorityEntity*>(m_TopmostEntity);
                if (Entity->HasBehaviour(CPriorityEntity::DeleteOnEscape)) {
                    RemovePriority(Entity);
                    Invalidate();
                    return;
                }
            }
            m_TopmostEntity->HandleKeyEvent(Key);
        }
    }

    void                Render(NVGcontext* VgContext) {
        if (m_RootLayer)                { m_RootLayer->Render(VgContext); }
        for (auto& e : m_EntityLayer)   { e->Render(VgContext); }
        for (auto& e : m_PriorityLayer) { e->Render(VgContext); }
    }

private:
    void                SetActive(CEntity* Entity) {
        for (auto& e : m_EntityLayer) { 
            e->SetActive(false);
        }
        Entity->SetActive(true);
        m_TopmostEntity = Entity;
    }

    void                SetPriorityActive(CPriorityEntity* Entity) {
        // ? for (auto& e : m_EntityLayer) { e->SetActive(false); }
        for (auto& e : m_PriorityLayer) { e->SetActive(false); }
        Entity->SetActive(true);
        m_TopmostEntity = Entity;
    }

    void                UpdateNextActive(CEntity* Removed) {
        if (Removed != m_TopmostEntity) {
            return;
        }

        // Do we have any priorities that should get shown?
        if (m_PriorityLayer.size() != 0) {
            SetPriorityActive(m_PriorityLayer.back().get());
        }
        else if (m_EntityLayer.size() != 0) {
            SetActive(m_EntityLayer.back().get());
        }
    }

    void                Invalidate() {
        sVioarr.UpdateNotify();
    }

    std::unique_ptr<CEntity>                    m_RootLayer;
    std::list<std::unique_ptr<CEntity>>         m_EntityLayer;
    std::list<std::unique_ptr<CPriorityEntity>> m_PriorityLayer;
    CEntity*                                    m_TopmostEntity;
};
