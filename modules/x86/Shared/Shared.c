/* MollenOS
*
* Copyright 2011 - 2014, Philip Meulengracht
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
* MollenOS Module Shared Library
*/

/* Includes */
#include <stddef.h>
#include <Arch.h>
#include <Driver.h>

/* Defined global */
Addr_t *GlbFunctionTable = NULL;

/* Typedefs */
typedef void(*__kpanic)(const char *Msg);
typedef void(*__stall)(uint32_t MilliSeconds);
typedef void(*__readtsc)(uint64_t *val);

void kernel_panic(const char *Msg)
{
	((__kpanic)GlbFunctionTable[kFuncKernelPanic])(Msg);
}

void StallMs(uint32_t MilliSeconds)
{
	((__stall)GlbFunctionTable[kFuncStall])(MilliSeconds);
}

void ReadTSC(uint64_t *Value)
{
	((__readtsc)GlbFunctionTable[kFuncReadTSC])(Value);
}

/* Devices */
#include <DeviceManager.h>

typedef DevId_t(*__regdevice)(char *Name, DeviceType_t Type, void *Data);
typedef void (*__unregdevice)(DevId_t DeviceId);

/* Register Device Wrapper */
DevId_t DmCreateDevice(char *Name, DeviceType_t Type, void *Data)
{
	return ((__regdevice)GlbFunctionTable[kFuncRegisterDevice])(Name, Type, Data);
}

/* Unregister Device Wrapper */
void DmDestroyDevice(DevId_t DeviceId)
{
	((__unregdevice)GlbFunctionTable[kFuncUnregisterDevice])(DeviceId);
}