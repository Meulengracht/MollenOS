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
#ifndef __VALI_OS_H__
#define __VALI_OS_H__

#include <os/osdefs.h>
#include <ddk/barrier.h>
#include <ddk/busdevice.h>
#include <ddk/io.h>
#include <ddk/utils.h>

#define ASSERT(x) if(!(x)) panic("EM: x")

#define usec_delay(x) DELAY(x)
#define usec_delay_irq(x) usec_delay(x)
#define msec_delay(x) DELAY(1000*(x))
#define msec_delay_irq(x) DELAY(1000*(x))

/* Enable/disable debugging statements in shared code */
#define DBG		0

#define DEBUGOUT(...) \
    do { if (DBG) TRACE(__VA_ARGS__); } while (0)
#define DEBUGOUT1(...)			DEBUGOUT(__VA_ARGS__)
#define DEBUGOUT2(...)			DEBUGOUT(__VA_ARGS__)
#define DEBUGOUT3(...)			DEBUGOUT(__VA_ARGS__)
#define DEBUGOUT7(...)			DEBUGOUT(__VA_ARGS__)
#define DEBUGFUNC(F)			DEBUGOUT(F "\n")

#define STATIC			static
#define FALSE			0
#define TRUE			1
#define CMD_MEM_WRT_INVALIDATE	0x0010  /* BIT_4 */
#define PCIC_EXPRESS 0x10

/* Mutex used in the shared code */
#define E1000_MUTEX                     struct mtx
#define E1000_MUTEX_INIT(mutex)         mtx_init((mutex), #mutex, \
                                            MTX_NETWORK_LOCK, \
					    MTX_DEF | MTX_DUPOK)
#define E1000_MUTEX_DESTROY(mutex)      mtx_destroy(mutex)
#define E1000_MUTEX_LOCK(mutex)         mtx_lock(mutex)
#define E1000_MUTEX_TRYLOCK(mutex)      mtx_trylock(mutex)
#define E1000_MUTEX_UNLOCK(mutex)       mtx_unlock(mutex)

typedef uint64_t	u64;
typedef uint32_t	u32;
typedef uint16_t	u16;
typedef uint8_t		u8;
typedef int64_t		s64;
typedef int32_t		s32;
typedef int16_t		s16;
typedef int8_t		s8;

#define __le16		u16
#define __le32		u32
#define __le64		u64

#if defined(__i386__) || defined(__amd64__)
static __inline
void prefetch(void *x)
{
	__asm volatile("prefetcht0 %0" :: "m" (*(unsigned long *)x));
}
#else
#define prefetch(x)
#endif

struct e1000_osdep
{
    DeviceIo_t*  mem_bus;
    DeviceIo_t*  flash_bus;
    DeviceIo_t*  io_bus;
    BusDevice_t* device;
};

#define E1000_REGISTER(hw, reg) (((hw)->mac.type >= e1000_82543) \
    ? (reg) : e1000_translate_register_82542(reg))

#define E1000_WRITE_FLUSH(a) E1000_READ_REG(a, E1000_STATUS)

/* Read from an absolute offset in the adapter's memory space */
#define E1000_READ_OFFSET(hw, offset) \
    ReadDeviceIo(((struct e1000_osdep *)(hw)->back)->mem_bus, offset, 4)

/* Write to an absolute offset in the adapter's memory space */
#define E1000_WRITE_OFFSET(hw, offset, value) \
    WriteDeviceIo(((struct e1000_osdep *)(hw)->back)->mem_bus, offset, value, 4)

/* Register READ/WRITE macros */
#define E1000_READ_REG(hw, reg) \
    ReadDeviceIo(((struct e1000_osdep *)(hw)->back)->mem_bus, E1000_REGISTER(hw, reg), 4)

#define E1000_WRITE_REG(hw, reg, value) \
    WriteDeviceIo(((struct e1000_osdep *)(hw)->back)->mem_bus, E1000_REGISTER(hw, reg), value, 4)

#define E1000_READ_REG_ARRAY(hw, reg, index) \
    ReadDeviceIo(((struct e1000_osdep *)(hw)->back)->mem_bus, E1000_REGISTER(hw, reg) + ((index)<< 2), 4)

#define E1000_WRITE_REG_ARRAY(hw, reg, index, value) \
    WriteDeviceIo(((struct e1000_osdep *)(hw)->back)->mem_bus, E1000_REGISTER(hw, reg) + ((index)<< 2), value, 4)

#define E1000_READ_REG_ARRAY_DWORD E1000_READ_REG_ARRAY
#define E1000_WRITE_REG_ARRAY_DWORD E1000_WRITE_REG_ARRAY

#define E1000_READ_REG_ARRAY_BYTE(hw, reg, index) \
    ReadDeviceIo(((struct e1000_osdep *)(hw)->back)->mem_bus, E1000_REGISTER(hw, reg) + (index), 1)

#define E1000_WRITE_REG_ARRAY_BYTE(hw, reg, index, value) \
    WriteDeviceIo(((struct e1000_osdep *)(hw)->back)->mem_bus, E1000_REGISTER(hw, reg) + (index), value, 1)

#define E1000_WRITE_REG_ARRAY_WORD(hw, reg, index, value) \
    WriteDeviceIo(((struct e1000_osdep *)(hw)->back)->mem_bus, E1000_REGISTER(hw, reg) + ((index) << 1), value, 2)

#define E1000_WRITE_REG_IO(hw, reg, value) do {\
    WriteDeviceIo(((struct e1000_osdep *)(hw)->back)->io_bus, (hw)->io_base, reg, 4); \
    WriteDeviceIo(((struct e1000_osdep *)(hw)->back)->io_bus, (hw)->io_base + 4, value, 4); } while (0)

#define E1000_READ_FLASH_REG(hw, reg) \
    ReadDeviceIo(((struct e1000_osdep *)(hw)->back)->flash_bus, reg, 4)

#define E1000_READ_FLASH_REG16(hw, reg) \
    ReadDeviceIo(((struct e1000_osdep *)(hw)->back)->flash_bus, reg, 2)

#define E1000_WRITE_FLASH_REG(hw, reg, value) \
    WriteDeviceIo(((struct e1000_osdep *)(hw)->back)->flash_bus, reg, value, 4)

#define E1000_WRITE_FLASH_REG16(hw, reg, value) \
    WriteDeviceIo(((struct e1000_osdep *)(hw)->back)->flash_bus, reg, value, 2)

#endif //!__VALI_OS_H__
