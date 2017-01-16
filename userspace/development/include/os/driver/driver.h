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
 * MollenOS Driver Inteface
 * - MollenOS SDK 
 */

#ifndef _MCORE_DRIVER_H_
#define _MCORE_DRIVER_H_

/* Includes
 * - C-Library */
#include <os/osdefs.h>

/* Includes
 * - System */
#include <os/driver/io.h>
#include <os/driver/acpi.h>
#include <os/driver/device.h>

/* These are the different IPC functions supported
 * by the driver, note that some of them might
 * be changed in the different versions, and/or new
 * functions will be added */
#define __DRIVER_REGISTERINSTANCE		IPC_DECL_FUNCTION(0)
#define __DRIVER_UNREGISTERINSTANCE		IPC_DECL_FUNCTION(1)
#define __DRIVER_INTERRUPT				IPC_DECL_FUNCTION(2)
#define __DRIVER_QUERY					IPC_DECL_FUNCTION(3)
#define __DRIVER_UNLOAD					IPC_DECL_FUNCTION(4)

/* OnLoad
 * The entry-point of a driver, this is called
 * as soon as the driver is loaded in the system */
#ifdef __DRIVER_EXPORT
__CRT_EXTERN OsStatus_t OnLoad(void);
#endif

/* OnUnload
 * This is called when the driver is being unloaded
 * and should free all resources allocated by the system */
#ifdef __DRIVER_EXPORT
__CRT_EXTERN OsStatus_t OnUnload(void);
#endif

/* OnRegister
 * Is called when the device-manager registers a new
 * instance of this driver for the given device */
#ifdef __DRIVER_EXPORT
__CRT_EXTERN OsStatus_t OnRegister(MCoreDevice_t *Device);
#else

#endif

/* OnUnregister
 * Is called when the device-manager wants to unload
 * an instance of this driver from the system */
#ifdef __DRIVER_EXPORT
__CRT_EXTERN OsStatus_t OnUnregister(MCoreDevice_t *Device);
#else

#endif

/* OnInterrupt
 * Is called when one of the registered devices
 * produces an interrupt. On successful handled
 * interrupt return OsNoError, otherwise the interrupt
 * won't be acknowledged */
#ifdef __DRIVER_EXPORT
__CRT_EXTERN OsStatus_t OnInterrupt(void);
#else

#endif

#endif //!_MCORE_DRIVER_H_
