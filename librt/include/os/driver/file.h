/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * MollenOS MCore - File Definitions & Structures
 * - This header describes the base file-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef _FILE_INTERFACE_H_
#define _FILE_INTERFACE_H_

/* Includes
 * - C-Library */
#include <os/osdefs.h>
#include <os/ipc/ipc.h>
#include <os/driver/server.h>

/* The export macro, and is only set by the
 * the actual implementation of the devicemanager */
#ifdef __FILEMANAGER_IMPL
#define __FILEAPI __CRT_EXTERN
#else
#define __FILEAPI static __CRT_INLINE
#endif

/* These definitions are in-place to allow a custom
 * setting of the file-manager, these are set to values
 * where in theory it should never be needed to have more */
#define __FILEMANAGER_INTERFACE_VERSION		1

/* These are the different IPC functions supported
 * by the fielmanager, note that some of them might
 * be changed in the different versions, and/or new
 * functions will be added */
#define __FILEMANAGER_REGISTERDISK				IPC_DECL_FUNCTION(0)
#define __FILEMANAGER_UNREGISTERDISK			IPC_DECL_FUNCTION(1)

#define __FILEMANAGER_OPENFILE					IPC_DECL_FUNCTION(2)
#define __FILEMANAGER_CLOSEFILE					IPC_DECL_FUNCTION(3)
#define __FILEMANAGER_DELETEFILE				IPC_DECL_FUNCTION(4)
#define __FILEMANAGER_READFILE					IPC_DECL_FUNCTION(5)
#define __FILEMANAGER_WRITEFILE					IPC_DECL_FUNCTION(6)
#define __FILEMANAGER_SEEKFILE					IPC_DECL_FUNCTION(7)
#define __FILEMANAGER_FLUSHFILE					IPC_DECL_FUNCTION(8)

/* RegisterDisk
 * */

/* UnregisterDisk
 * */

/* OpenFile
 * */

/* CloseFile
 * */

/* ReadFile
 * */

/* WriteFile
 * */

/* SeekFile
 * */

/* FlushFile
 * */

#endif //!_DEVICE_INTERFACE_H_
