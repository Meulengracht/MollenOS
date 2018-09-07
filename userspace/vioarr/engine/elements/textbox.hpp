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
#include "../entity.hpp"
#include <sstream> 
#include <string>

class CTextBox : public CEntity {
public:
    CTextBox(CEntity* Parent, NVGcontext* VgContext, int Width, int Height);
    CTextBox(NVGcontext* VgContext, int Width, int Height);
    ~CTextBox();

    void SetIcon(const std::string& IconPath);
    void SetPlaceholderText(const std::string& Text);
    void SetPasswordField(bool HideText, char PassCharacter = '*');
    void Add(char Character);
    void Remove();

protected:
    // Override the inherited methods
    void Draw(NVGcontext* VgContext);

private:
    std::stringstream   m_InputBuffer;
    std::string         m_PlaceholderText;
    int                 m_Width;
    int                 m_Height;
    bool                m_IsPassword;
    char                m_PasswordCharacter;

    std::string         m_LastText;
    NVGcolor            m_LastColor;
    float               m_OffsetX;
};
