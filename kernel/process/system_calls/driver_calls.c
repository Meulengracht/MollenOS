/* MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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
 * MollenOS MCore - System Calls
 */
#define __MODULE "SCIF"
//#define __TRACE

#include <process/phoenix.h>
#include <process/server.h>
#include <modules/modules.h>
#include <acpiinterface.h>
#include <interrupts.h>
#include <deviceio.h>
#include <os/input.h>
#include <os/acpi.h>
#include <machine.h>
#include <timers.h>
#include <debug.h>
#include <heap.h>
#include <pipe.h>

// Externs
extern OsStatus_t ScRpcExecute(MRemoteCall_t* RemoteCall, int Async);

/* ScAcpiQueryStatus
 * Queries basic acpi information and returns either OsSuccess
 * or OsError if Acpi is not supported on the running platform */
OsStatus_t
ScAcpiQueryStatus(
   _In_ AcpiDescriptor_t*   AcpiDescriptor)
{
    if (AcpiDescriptor == NULL) {
        return OsError;
    }

    if (AcpiAvailable() == ACPI_NOT_AVAILABLE) {
        return OsError;
    }
    else {
        AcpiDescriptor->Century         = AcpiGbl_FADT.Century;
        AcpiDescriptor->BootFlags       = AcpiGbl_FADT.BootFlags;
        AcpiDescriptor->ArmBootFlags    = AcpiGbl_FADT.ArmBootFlags;
        AcpiDescriptor->Version         = ACPI_VERSION_6_0;
        return OsSuccess;
    }
}

/* ScAcpiQueryTableHeader
 * Queries the table header of the table that matches
 * the given signature, if none is found OsError is returned */
OsStatus_t
ScAcpiQueryTableHeader(
    _In_ const char*        Signature,
    _In_ ACPI_TABLE_HEADER* Header)
{
    ACPI_TABLE_HEADER *PointerToHeader = NULL;

    // Sanitize some statuses
    if (AcpiAvailable() == ACPI_NOT_AVAILABLE) {
        return OsError;
    }
    if (ACPI_FAILURE(AcpiGetTable((ACPI_STRING)Signature, 0, &PointerToHeader))) {
        return OsError;
    }

    memcpy(Header, PointerToHeader, sizeof(ACPI_TABLE_HEADER));
    return OsSuccess;
}

/* ScAcpiQueryTable
 * Queries the full table information of the table that matches
 * the given signature, if none is found OsError is returned */
OsStatus_t
ScAcpiQueryTable(
    _In_ const char*        Signature,
    _In_ ACPI_TABLE_HEADER* Table)
{
    ACPI_TABLE_HEADER *Header = NULL;

    // Sanitize some statuses
    if (AcpiAvailable() == ACPI_NOT_AVAILABLE) {
        return OsError;
    }
    if (ACPI_FAILURE(AcpiGetTable((ACPI_STRING)Signature, 0, &Header))) {
        return OsError;
    }

    memcpy(Header, Table, Header->Length);
    return OsSuccess;
}

/* ScAcpiQueryInterrupt 
 * Queries the interrupt-line for the given bus, device and
 * pin combination. The pin must be zero indexed. Conform flags
 * are returned in the <AcpiConform> */
OsStatus_t
ScAcpiQueryInterrupt(
    _In_  DevInfo_t         Bus,
    _In_  DevInfo_t         Device,
    _In_  int               Pin, 
    _Out_ int*              Interrupt,
    _Out_ Flags_t*          AcpiConform)
{
    *Interrupt = AcpiDeriveInterrupt(Bus, Device, Pin, AcpiConform);
    return (*Interrupt == INTERRUPT_NONE) ? OsError : OsSuccess;
}
 
/* ScIoSpaceRegister
 * Creates and registers a new IoSpace with our
 * architecture sub-layer, it must support io-spaces 
 * or atleast dummy-implementation */
OsStatus_t
ScIoSpaceRegister(
    _In_ DeviceIo_t*        IoSpace)
{
    return RegisterSystemDeviceIo(IoSpace);
}

/* ScIoSpaceAcquire
 * Tries to claim a given io-space, only one driver
 * can claim a single io-space at a time, to avoid
 * two drivers using the same device */
OsStatus_t
ScIoSpaceAcquire(
    _In_ DeviceIo_t*        IoSpace)
{
    return AcquireSystemDeviceIo(IoSpace);
}

/* ScIoSpaceRelease
 * Tries to release a given io-space, only one driver
 * can claim a single io-space at a time, to avoid
 * two drivers using the same device */
OsStatus_t
ScIoSpaceRelease(
    _In_ DeviceIo_t*        IoSpace)
{
    return ReleaseSystemDeviceIo(IoSpace);
}

/* ScIoSpaceDestroy
 * Destroys the io-space with the given id and removes
 * it from the io-manage in the operation system, it
 * can only be removed if its not already acquired */
OsStatus_t
ScIoSpaceDestroy(
    _In_ DeviceIo_t*        IoSpace)
{
    return DestroySystemDeviceIo(IoSpace);
}

/* Allows a server to register an alias for its 
 * process id, as applications can't possibly know
 * its id if it changes */
OsStatus_t
ScRegisterAliasId(
    _In_ UUId_t             Alias)
{
    // Debug
    TRACE("ScRegisterAliasId(Server %s, Alias 0x%X)",
        MStringRaw(PhoenixGetCurrentAsh()->Name), Alias);
    return PhoenixRegisterAlias(PhoenixGetCurrentAsh(), Alias);
}

/* ScLoadDriver
 * Attempts to resolve the best possible drive for
 * the given device information */
OsStatus_t
ScLoadDriver(
    _In_ MCoreDevice_t*     Device,
    _In_ size_t             Length)
{
    // Variables
    MCorePhoenixRequest_t *Request  = NULL;
    MCoreServer_t *Server           = NULL;
    MCoreModule_t *Module           = NULL;
    MString_t *Path                 = NULL;
    MRemoteCall_t RemoteCall        = { { 0 }, { 0 }, 0 };

    // Trace
    TRACE("ScLoadDriver(Vid 0x%x, Pid 0x%x, Class 0x%x, Subclass 0x%x)",
        Device->VendorId, Device->DeviceId,
        Device->Class, Device->Subclass);

    // Sanitize parameters, length must not be less than base
    if (Device == NULL || Length < sizeof(MCoreDevice_t)) {
        return OsError;
    }

    // First of all, if a server has already been spawned
    // for the specific driver, then call it's RegisterInstance
    Server = PhoenixGetServerByDriver(
        Device->VendorId, Device->DeviceId,
        Device->Class, Device->Subclass);

    // Sanitize the lookup 
    // If it's not found, spawn server
    if (Server == NULL) {
        // Look for matching driver first, then generic
        Module = ModulesFindSpecific(Device->VendorId, Device->DeviceId);
        if (Module == NULL) {
            Module = ModulesFindGeneric(Device->Class, Device->Subclass);
        }
        if (Module == NULL) {
            return OsError;
        }

        // Build ramdisk path for module/server
        Path = MStringCreate("rd:/", StrUTF8);
        MStringAppendString(Path, Module->Name);

        // Create the request 
        Request = (MCorePhoenixRequest_t*)kmalloc(sizeof(MCorePhoenixRequest_t));
        memset(Request, 0, sizeof(MCorePhoenixRequest_t));
        Request->Base.Type = AshSpawnServer;
        Request->Path = Path;

        // Initiate request
        PhoenixCreateRequest(Request);
        PhoenixWaitRequest(Request, 0);

        // Sanitize startup
        Server = PhoenixGetServer(Request->AshId);
        assert(Server != NULL);

        // Cleanup
        MStringDestroy(Request->Path);
        kfree(Request);

        // Update the server params for next load
        Server->VendorId = Device->VendorId;
        Server->DeviceId = Device->DeviceId;
        Server->DeviceClass = Device->Class;
        Server->DeviceSubClass = Device->Subclass;
    }

    // Initialize the base of a new message, always protocol version 1
    RPCInitialize(&RemoteCall, Server->Base.Id, 1, __DRIVER_REGISTERINSTANCE);
    RPCSetArgument(&RemoteCall, 0, Device, Length);

    // Make sure the server has opened it's comm-pipe
    PhoenixWaitAshPipe(&Server->Base, PIPE_REMOTECALL);
    return ScRpcExecute(&RemoteCall, 1);
}

/* ScRegisterInterrupt 
 * Allocates the given interrupt source for use by
 * the requesting driver, an id for the interrupt source
 * is returned. After a succesful register, OnInterrupt
 * can be called by the event-system */
UUId_t
ScRegisterInterrupt(
    _In_ DeviceInterrupt_t* Interrupt,
    _In_ Flags_t            Flags)
{
    if (Interrupt == NULL
        || (Flags & (INTERRUPT_KERNEL | INTERRUPT_SOFT))) {
        return UUID_INVALID;
    }
    return InterruptRegister(Interrupt, Flags);
}

/* ScUnregisterInterrupt 
 * Unallocates the given interrupt source and disables
 * all events of OnInterrupt */
OsStatus_t
ScUnregisterInterrupt(
    _In_ UUId_t             Source)
{
    return InterruptUnregister(Source);
}

/* ScKeyEvent
 * Handles key notification by redirecting them to the standard input in the system. */
OsStatus_t
ScKeyEvent(
    _In_ SystemKey_t*       Key)
{
    if (GetMachine()->StdInput != NULL) {
        return WriteSystemPipe(GetMachine()->StdInput, (const uint8_t*)Key, sizeof(SystemKey_t));
    }
    return OsSuccess;
}

/* ScInputEvent
 * Handles input notification by redirecting them to the window manager input. */
OsStatus_t
ScInputEvent(
    _In_ SystemInput_t*     Input)
{
    if (GetMachine()->WmInput != NULL) {
        return WriteSystemPipe(GetMachine()->WmInput, (const uint8_t*)Input, sizeof(SystemInput_t));
    }
    return OsSuccess;
}

/* ScTimersStart
 * Creates a new standard timer for the requesting process. 
 * When interval elapses a __TIMEOUT event is generated for
 * the owner of the timer. */
UUId_t
ScTimersStart(
    _In_ size_t             Interval,
    _In_ int                Periodic,
    _In_ const void*        Data)
{
    return TimersStart(Interval, Periodic, Data);
}

/* ScTimersStop
 * Destroys a existing standard timer, owner must be the requesting
 * process. Otherwise access fault. */
OsStatus_t
ScTimersStop(
    _In_ UUId_t             TimerId) {
    return TimersStop(TimerId);
}
