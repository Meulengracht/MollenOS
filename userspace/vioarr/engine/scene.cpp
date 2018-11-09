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

#include "utils/log_manager.hpp"
#include "scene.hpp"

CScene::CScene(CEntity* RootLayer) 
    : m_TopmostElementId(-1), m_NullElement(nullptr), m_NullPriorityElement(nullptr)
{
    m_RootLayer.reset(RootLayer);
}

CScene::~CScene()
{
    m_EntityLayer.clear();
    m_PriorityLayer.clear();
}

void CScene::Add(CEntity* Entity)
{
    sLog.Info("CScene::Add");
    std::unique_lock<std::mutex> LockedSection(m_Lock);
    m_EntityLayer.push_back(std::unique_ptr<CEntity>(Entity));
    if (m_PriorityLayer.size() == 0) {
        UpdateActiveElement<CEntity>(m_EntityLayer);
    }
    InvalidateScreen();
}

bool CScene::Remove(long ElementId)
{
    sLog.Info("CScene::Remove");
    auto& Element = GetElement(ElementId);
    if (Element) {
        std::unique_lock<std::mutex> LockedSection(m_Lock);
        m_EntityLayer.remove(Element);
        LockedSection.unlock();
        
        UpdateNextActive(ElementId);
        InvalidateScreen();
        return true;
    }
    return false;
}

void CScene::MoveToFront(long ElementId)
{
    sLog.Info("CScene::MoveToFront");
    std::unique_lock<std::mutex> LockedSection(m_Lock);
    auto i = m_EntityLayer.begin();
    while (i != m_EntityLayer.end()) {
        if ((*i)->GetId() == ElementId) {
            break;
        }
        i++;
    }
    m_EntityLayer.splice(m_EntityLayer.end(), m_EntityLayer, i);
    UpdateActiveElement<CEntity>(m_EntityLayer);
    InvalidateScreen();
}

long CScene::GetEntityIdForOwner(UUId_t Owner)
{
    sLog.Info("CScene::GetEntityIdForOwner");
    std::lock_guard Guard(m_Lock);
    for (auto& e : m_EntityLayer) {
        if (e->GetOwner() == Owner) {
            return e->GetId();
        }
    }
    return -1;
}

bool CScene::HasEntity(long ElementId)
{
    sLog.Info("CScene::HasEntity");
    auto& Element = GetElement(ElementId);
    if (Element) {
        return true;
    }
    return false;
}

void CScene::AddPriority(CPriorityEntity* Priority)
{
    sLog.Info("CScene::AddPriority");
    std::unique_lock<std::mutex> LockedSection(m_Lock);
    // Remove all entities that die upon lost focus
    m_PriorityLayer.remove_if([](std::unique_ptr<CPriorityEntity> const& e) { return e->HasBehaviour(CPriorityEntity::DeleteOnFocusLost); });
    m_PriorityLayer.push_back(std::unique_ptr<CPriorityEntity>(Priority));
    UpdateActiveElement<CPriorityEntity>(m_PriorityLayer);
    InvalidateScreen();
}

bool CScene::RemovePriority(long ElementId)
{
    sLog.Info("CScene::RemovePriority %i", ElementId);
    auto& Element = GetPriorityElement(ElementId);
    if (Element) {
        std::unique_lock<std::mutex> LockedSection(m_Lock);
        m_PriorityLayer.remove(Element);
        LockedSection.unlock();

        UpdateNextActive(ElementId);
        InvalidateScreen();
        return true;
    }
    return false;
}

void CScene::ProxyKeyEvent(SystemKey_t* Key)
{
    sLog.Info("CScene::ProxyKeyEvent");
    if (m_TopmostElementId != -1) {
        auto& PriorityElement = GetPriorityElement(m_TopmostElementId);
        if (PriorityElement) {
            if (Key->KeyCode == VK_ESCAPE) {
                if (PriorityElement->HasBehaviour(CPriorityEntity::DeleteOnEscape)) {
                    RemovePriority(m_TopmostElementId);
                    return;
                }
            }
            PriorityElement->HandleKeyEvent(Key);
        }
        else
        {
            auto& Element = GetElement(m_TopmostElementId);
            if (Element) {
                Element->HandleKeyEvent(Key);
            }
            else {
                m_TopmostElementId = -1; 
            }
        }
    }
}

bool CScene::InvalidateElement(long ElementId)
{
    sLog.Info("CScene::InvalidateElement");
    auto& Element = GetElement(ElementId);
    if (Element) {
        Element->Invalidate();
        return true;
    }
    return false;
}

void CScene::Render(NVGcontext* VgContext)
{
    sLog.Info("CScene::Render");
    std::lock_guard Guard(m_Lock);
    if (m_RootLayer)                { m_RootLayer->Render(VgContext); }
    for (auto& e : m_EntityLayer)   { e->Render(VgContext); }
    for (auto& e : m_PriorityLayer) { e->Render(VgContext); }
}

template<class T>
void CScene::UpdateActiveElement(const std::list<std::unique_ptr<T>>& ElementList)
{
    auto& NextActive = ElementList.back();
    sLog.Info("CScene::UpdateActiveElement %i", NextActive->GetId());
    for (auto& e : ElementList) { e->SetActive(false); }
    NextActive->SetActive(true);
    m_TopmostElementId = NextActive->GetId();
}

void CScene::UpdateNextActive(long RemovedElementId)
{
    sLog.Info("CScene::UpdateNextActive %i, %i", RemovedElementId, m_TopmostElementId);
    if (RemovedElementId != m_TopmostElementId) {
        return;
    }
    m_TopmostElementId = -1;

    // Do we have any priorities that should get shown?
    if (m_PriorityLayer.size() != 0) {
        UpdateActiveElement<CPriorityEntity>(m_PriorityLayer);
    }
    else if (m_EntityLayer.size() != 0) {
        UpdateActiveElement<CEntity>(m_EntityLayer);
    }
}

const std::unique_ptr<CEntity>& CScene::GetElement(long ElementId)
{
    std::lock_guard Guard(m_Lock);
    for (auto& e : m_EntityLayer) {
        if (e->GetId() == ElementId) {
            return e;
        }
    }
    return m_NullElement;
}

const std::unique_ptr<CPriorityEntity>& CScene::GetPriorityElement(long ElementId)
{
    std::lock_guard Guard(m_Lock);
    for (auto& e : m_PriorityLayer) {
        if (e->GetId() == ElementId) {
            return e;
        }
    }
    return m_NullPriorityElement;
}

void CScene::InvalidateScreen()
{
    sLog.Info("CScene::InvalidateScreen");
    sVioarr.UpdateNotify();
}
