/******************************************************************************

  Copyright (c) 2001-2019, Intel Corporation
  All rights reserved.
  
  Redistribution and use in source and binary forms, with or without 
  modification, are permitted provided that the following conditions are met:
  
   1. Redistributions of source code must retain the above copyright notice, 
      this list of conditions and the following disclaimer.
  
   2. Redistributions in binary form must reproduce the above copyright 
      notice, this list of conditions and the following disclaimer in the 
      documentation and/or other materials provided with the distribution.
  
   3. Neither the name of the Intel Corporation nor the names of its 
      contributors may be used to endorse or promote products derived from 
      this software without specific prior written permission.
  
  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE 
  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE.

******************************************************************************/

#include "e1000_api.h"

/*
 * NOTE: the following routines using the e1000 
 * 	naming style are provided to the shared
 *	code but are OS specific
 */

void
e1000_write_pci_cfg(struct e1000_hw *hw, u32 reg, u16 *value)
{
    size_t largeValue = *value;
    IoctlDeviceEx(
            ((struct e1000_osdep *)hw->back)->device->Base.Id,
            1,
            reg,
            &largeValue,
            2
    );
}

void
e1000_read_pci_cfg(struct e1000_hw *hw, u32 reg, u16 *value)
{
    size_t largeValue;
    IoctlDeviceEx(
            ((struct e1000_osdep *)hw->back)->device->Base.Id,
            0,
            reg,
            &largeValue,
            2
    );
	*value = LOWORD(largeValue);
}

void
e1000_pci_set_mwi(struct e1000_hw *hw)
{
    u16 value = hw->bus.pci_cmd_word | CMD_MEM_WRT_INVALIDATE;
    e1000_write_pci_cfg(hw, 0x4, &value);
}

void
e1000_pci_clear_mwi(struct e1000_hw *hw)
{
    u16 value = hw->bus.pci_cmd_word & ~CMD_MEM_WRT_INVALIDATE;
    e1000_write_pci_cfg(hw, 0x4, &value);
}

void e1000_pci_find_cap(struct e1000_hw *hw, u8 cap, u32* offsetOut)
{
    uint16_t capIndex;
    uint16_t capId;

    // read the capabilities pointer
    e1000_read_pci_cfg(hw, 0x34, &capIndex);
    if (capIndex == 0) {
        *offsetOut = 0;
        return;
    }

    // read the first capability
    e1000_read_pci_cfg(hw, capIndex, &capId);
    while (capId != 0) {
        if (capId == cap) {
            *offsetOut = capIndex;
            return;
        }

        e1000_read_pci_cfg(hw, capIndex + 1, &capIndex);
    }

    *offsetOut = 0;
}

/*
 * Read the PCI Express capabilities
 */
int32_t
e1000_read_pcie_cap_reg(struct e1000_hw *hw, u32 reg, u16 *value)
{
	u32	offset;

    e1000_pci_find_cap(hw, PCIC_EXPRESS, &offset);
	e1000_read_pci_cfg(hw, offset + reg, value);
	return (E1000_SUCCESS);
}

/*
 * Write the PCI Express capabilities
 */
int32_t
e1000_write_pcie_cap_reg(struct e1000_hw *hw, u32 reg, u16 *value)
{
	u32	offset;

    e1000_pci_find_cap(hw, PCIC_EXPRESS, &offset);
    e1000_write_pci_cfg(hw, offset + reg, value);
	return (E1000_SUCCESS);
}
