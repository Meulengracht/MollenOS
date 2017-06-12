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
 * MollenOS MCore - USB Controller Manager
 * - Contains the implementation of a shared controller manager
 *   for all the usb drivers
 */
//#define __TRACE

/* Includes
 * - System */
#include <os/mollenos.h>
#include <os/thread.h>
#include <os/utils.h>
#include "manager.h"

/* Includes
 * - Library */
#include <stddef.h>
#include <stdlib.h>

/* Globals
 * Keeps track of the usb-manager state and its data */
static List_t *__GlbControllers = NULL;
static UUId_t __GlbTransferId = 0;

/* UsbManagerInitialize
 * Initializes the usb manager that keeps track of
 * all controllers and all attached devices */
OsStatus_t
UsbManagerInitialize(void)
{
	// Instantiate and reset variables
	__GlbControllers = ListCreate(KeyInteger, LIST_SAFE);
	__GlbTransferId = 0;

	// Done
	return OsSuccess;
}

/* UsbManagerDestroy
 * Cleans up the manager and releases resources allocated */
OsStatus_t
UsbManagerDestroy(void)
{
	// It's important cleanup in transactions are done
	// before this function is called
}

/* UsbManagerGetControllers
 * Retrieve a list of all attached controllers to the system. */
List_t*
UsbManagerGetControllers(void)
{
	// Retrieve a list pointer
	return __GlbControllers;
}

/* UsbManagerCreateTransfer
 * Creates a new transfer with the usb-manager.
 * Identifies and registers with neccessary services */
UsbManagerTransfer_t*
UsbManagerCreateTransfer(
	_In_ UsbTransfer_t *Transfer,
	_In_ UUId_t Requester,
	_In_ int ResponsePort,
	_In_ UUId_t Device,
	_In_ UUId_t Pipe)
{
	
}

/* UsbManagerGetTransfer
 * Retrieves a single transfer from the given transfer-id
 * the id is unique so no other information is needed. */
UsbManagerTransfer_t*
UsbManagerGetTransfer(
	_In_ UUId_t Id)
{

}

/* UsbManagerCreateController
 * Registers a new controller with the usb-manager.
 * Identifies and registers with neccessary services */
OsStatus_t
UsbManagerCreateController(
	_In_ void *Controller)
{

}

/* UsbManagerDestroyController
 * Unregisters a controller with the usb-manager.
 * Identifies and unregisters with neccessary services */
OsStatus_t
UsbManagerDestroyController(
	_In_ void *Controller)
{

}

/* UsbManagerGetController 
 * Returns a controller by the given device-id */
void*
UsbManagerGetController(
	_In_ UUId_t Device)
{

}

/* UsbManagerGetToggle 
 * Retrieves the toggle status for a given pipe */
int
UsbManagerGetToggle(
	_In_ UUId_t Device,
	_In_ UUId_t Pipe)
{

}

/* UsbManagetSetToggle 
 * Updates the toggle status for a given pipe */
void
UsbManagerSetToggle(
	_In_ UUId_t Device,
	_In_ UUId_t Pipe,
	_In_ int Toggle)
{

}
