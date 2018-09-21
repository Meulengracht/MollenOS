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

#include "suggestionbox.hpp"
#include "sprite.hpp"

#define BOX_BORDER_COLOR            103, 103, 103, 255
#define BOX_FILL_COLOR              255, 255, 255, 255

#define BOX_TEXT_PLACEHOLDER_COLOR  103, 103, 103, 255
#define BOX_TEXT_COLOR              0, 0, 0, 255

CSuggestionBox::CSuggestionBox(CEntity* Parent, NVGcontext* VgContext, int Width, int Height) 
    : CEntity(Parent, VgContext), m_PlaceholderText("Search"), m_Width(Width), m_Height(Height)
{
    // Create icon resource
    auto SearchIcon = new CSprite(this, VgContext, "$sys/themes/default/search16.png", 16, 16);
    SearchIcon->SetPosition(4.0f, 4.0f, 0.0f);
}

CSuggestionBox::CSuggestionBox(NVGcontext* VgContext, int Width, int Height) 
    : CSuggestionBox(nullptr, VgContext, Width, Height) { }

CSuggestionBox::~CSuggestionBox() { }

void CSuggestionBox::SetPlaceholderText(const std::string& Text) {
    m_PlaceholderText = Text;
}

void CSuggestionBox::Add(char Character) {
    m_InputBuffer << Character;
    m_LastUpdated = m_InputBuffer.str();
}
void CSuggestionBox::Remove() {
    if (m_InputBuffer.tellg() > 0) {
        m_InputBuffer.seekp(-1, m_InputBuffer.cur);
    }
    m_LastUpdated = m_InputBuffer.str();
}

void CSuggestionBox::Draw(NVGcontext* VgContext) {
    // Fill
	nvgBeginPath(VgContext);
	nvgRoundedRect(VgContext, 0.0f, 0.0f, m_Width, m_Height, 5.0f);
	nvgFillColor(VgContext, nvgRGBA(BOX_FILL_COLOR));
	nvgFill(VgContext);

	// Border
	nvgBeginPath(VgContext);
	nvgRoundedRect(VgContext, 0.0f, 0.0f, m_Width, m_Height, 5.0f);
    nvgStrokeWidth(VgContext, 1.0f);
    nvgStrokeColor(VgContext, nvgRGBA(BOX_BORDER_COLOR));
    nvgStroke(VgContext);

    // Text
    nvgFontSize(VgContext,  14.0f);
    nvgFontFace(VgContext,  "sans-normal");
	nvgTextAlign(VgContext, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
    if (m_LastUpdated.length() > 0) {
	    nvgFillColor(VgContext, nvgRGBA(BOX_TEXT_COLOR));
        nvgText(VgContext, 24.0f, 8.0f, m_LastUpdated.c_str(), NULL);
    }
    else {
	    nvgFillColor(VgContext, nvgRGBA(BOX_TEXT_PLACEHOLDER_COLOR));
        nvgText(VgContext, 24.0f, 8.0f, m_PlaceholderText.c_str(), NULL);
    }

    // Draw suggestions
}
