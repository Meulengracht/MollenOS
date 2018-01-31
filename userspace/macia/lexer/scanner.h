/* The Macia Language (MACIA)
*
* Copyright 2016, Philip Meulengracht
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
* Macia - Scanner (Lexer)
*/
#pragma once

/* Includes */
#include "../shared/element.h"
#include <vector>

/* The class 
 * Scans a file and breaks it down
 * into elements, this also filters all 
 * unneccessary bullshit from the file */
class Scanner
{
public:
	Scanner();
	~Scanner();

	/* Parse file */
	int Scan(char *Data, size_t Length);

	/* Retrieve elements */
	std::vector<Element*> &GetElements() { return m_lElements; }

private:
	/* Private - Functions */
	void CreateElement(ElementType_t Type, const char *Data, int Line, long Character);

	/* Private - Data */
	std::vector<Element*> m_lElements;
};

