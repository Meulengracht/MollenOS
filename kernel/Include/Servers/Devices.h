/* MollenOS
 *
 * Copyright 2011 - 2016, Philip Meulengracht
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
 * MollenOS MCore - Device Manager
 * - Initialization + Event Mechanism
 */

#ifndef _MCORE_SERVER_DEVMANAGER_H_
#define _MCORE_SERVER_DEVMANAGER_H_

/* Includes 
 * - C-Library */
#include <os/osdefs.h>
#include <os/ipc.h>
#include <ds/mstring.h>
#include <stddef.h>

/* Includes
 * - DRVM */
#include <Devices/Definitions.h>
#include <Devices/Device.h>

/* Device Request Types
 * These are the possible requests
 * to make for the different devices */
typedef enum _MCoreDeviceRequestType
{
	DrvmRequestRead,
	DrvmRequestWrite,
	DrvmRequestQuery

} MCoreDeviceRequestType_t;

/* Device IPC Event System
 * This is the request structure for
 * making any device related requests */
typedef struct _MCoreDeviceRequest
{
	/* IPC Base */
	MEventMessageBase_t Base;

	/* Type of request */
	MCoreDeviceRequestType_t Type;

	/* Device data (params)
	 * This determines which device get's the request */
	DevId_t DeviceId;

	/* Buffer data (params)
	 * This is used by read, write and query for buffers */
	uint8_t *Buffer;

	/* Value data (params)
	 * Which and how this is used is based on request
	 * This supports up to 64 bit */
	union {
		union {
			uint32_t Length;
			DevId_t DiskId;
			VfsQueryFunction_t Function;
			VfsFileFlags_t Flags;
			int Copy;
		} Lo;
		union {
			uint32_t Length;
			int Forced;
		} Hi;
	} Value;

	/* Error code if anything
	 * happened during ops */
	OsStatus_t Error;

} MCoreDeviceRequest_t;

#endif //!_MCORE_SERVER_DEVMANAGER_H_
