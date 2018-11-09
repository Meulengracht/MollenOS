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
#include <mutex>
#include <list>
#include "entity.hpp"

class CScene {
public:
    CScene(CEntity* RootLayer);
    ~CScene();

    void Add(CEntity* Entity);
    bool Remove(long ElementId);
    void MoveToFront(long ElementId);

    void AddPriority(CPriorityEntity* Priority);
    bool RemovePriority(long ElementId);

    long GetEntityIdForOwner(UUId_t Owner);
    bool HasEntity(long ElementId);
    void ProxyKeyEvent(SystemKey_t* Key);

    bool InvalidateElement(long ElementId);
    void Render(NVGcontext* VgContext);

private:
    template<class T>
    void UpdateActiveElement(const std::list<std::unique_ptr<T>>& ElementList);
    void UpdateNextActive(long RemovedElementId);
    void InvalidateScreen();

    const std::unique_ptr<CEntity>&         GetElement(long ElementId);
    const std::unique_ptr<CPriorityEntity>& GetPriorityElement(long ElementId);

private:
    std::mutex                                  m_Lock;
    std::unique_ptr<CEntity>                    m_RootLayer;
    std::list<std::unique_ptr<CEntity>>         m_EntityLayer;
    std::list<std::unique_ptr<CPriorityEntity>> m_PriorityLayer;
    long                                        m_TopmostElementId;

    std::unique_ptr<CEntity>                    m_NullElement;
    std::unique_ptr<CPriorityEntity>            m_NullPriorityElement;
};
