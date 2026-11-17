/**
 * Copyright, Philip Meulengracht
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __ARM64_ACPI_IORT_PRIVATE_H__
#define __ARM64_ACPI_IORT_PRIVATE_H__

#include "iort.h"

// Cap the node and mapping counts we accept from firmware. Checking for loops
// and overlapping ID ranges compares entries against one another, so a fixed
// limit keeps memory use bounded and stops bad tables from slowing down boot.
#define ARM_IORT_MAX_NODES  256U
#define ARM_IORT_MAX_RANGES 256U

// Flat storage backing ArmIortCatalogue_t. Each node's Mappings pointer slices
// directly into the shared Mappings array here instead of allocating separately.
struct ArmIortCatalogue {
    uint32_t         NodeCount;
    ArmIortNode_t    Nodes[ARM_IORT_MAX_NODES];
    ArmIortMapping_t Mappings[ARM_IORT_MAX_RANGES];
};

#endif //!__ARM64_ACPI_IORT_PRIVATE_H__
