/**
 * Copyright 2022, Philip Meulengracht
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
 *
 */

#include <os/services/mount.h>
#include <served/application.h>
#include <stdlib.h>
#include <io.h>

// /data/served/apps/<publisher>.<package>.pack
static mstring_t* __ApplicationPackPath(struct Application* application)
{
    return mstr_fmt(
            "/data/served/apps/%ms.%ms.pack",
            application->Publisher,
            application->Package
    );
}

// /data/served/mount/<name>
static mstring_t* __ApplicationMountPath(struct Application* application)
{
    return mstr_fmt(
            "/data/served/mount/%ms.%ms",
            application->Publisher,
            application->Package
    );
}

// /data/served/mount/<name>/<cmd>
static mstring_t* __ApplicationCommandPath(struct Application* application, struct Command* command)
{
    return mstr_fmt(
            "/data/served/mount/%ms.%ms/%ms",
            application->Publisher,
            application->Package,
            command->Path
    );
}

// /apps/<pub.app.cmd>
static mstring_t* __ApplicationCommandSymlinkPath(struct Application* application, struct Command* command)
{
    return mstr_fmt(
            "/apps/%ms.%ms.%ms",
            application->Publisher,
            application->Package,
            command->Name
    );
}

struct Command*
CommandNew(
        _In_ struct Application* application,
        _In_ mstring_t*          name,
        _In_ mstring_t*          path,
        _In_ mstring_t*          arguments,
        _In_ int                 type)
{
    struct Command* command;

    command = malloc(sizeof(struct Command));
    if (command == NULL) {
        return NULL;
    }

    ELEMENT_INIT(&command->ListHeader, 0, 0);
    command->Name = name;
    command->Path = path;
    command->Arguments = arguments;
    command->Type = type;

    // Generate some runtime paths here
    command->MountedPath = __ApplicationCommandPath(application, command);
    command->SymlinkPath = __ApplicationCommandSymlinkPath(application, command);
    if (application->PackPath == NULL || application->MountPath == NULL) {
        CommandDelete(command);
        return NULL;
    }

    // We associate the command with the application as a favor to our caller
    list_append(&application->Commands, &command->ListHeader);
    return command;
}

void
CommandDelete(
        _In_ struct Command* command)
{
    if (command == NULL) {
        return;
    }
    mstr_delete(command->Name);
    mstr_delete(command->Path);
    mstr_delete(command->Arguments);
    mstr_delete(command->MountedPath);
    mstr_delete(command->SymlinkPath);
    free(command);
}

struct Application*
ApplicationNew(
        _In_ mstring_t* name,
        _In_ mstring_t* publisher,
        _In_ mstring_t* package,
        _In_ int        major,
        _In_ int        minor,
        _In_ int        patch,
        _In_ int        revision)
{
    struct Application* application;

    application = malloc(sizeof(struct Application));
    if (application == NULL) {
        return NULL;
    }

    ELEMENT_INIT(&application->ListHeader, 0, 0);
    application->Name = name;
    application->Publisher = publisher;
    application->Package = package;
    application->Major = major;
    application->Minor = minor;
    application->Patch = patch;
    application->Revision = revision;
    list_construct(&application->Commands);

    // Generate paths for the application
    application->PackPath  = __ApplicationPackPath(application);
    application->MountPath = __ApplicationMountPath(application);
    if (application->PackPath == NULL || application->MountPath == NULL) {
        ApplicationDelete(application);
        return NULL;
    }
    return application;
}

static void __FreeCommand(
        _In_ element_t* item,
        _In_ void*      context)
{
    struct Command* command = (struct Command*)item;
    _CRT_UNUSED(context);

    CommandDelete(command);
}

void
ApplicationDelete(
        _In_ struct Application* application)
{
    if (application == NULL) {
        return;
    }

    mstr_delete(application->Name);
    mstr_delete(application->Publisher);
    mstr_delete(application->Package);
    mstr_delete(application->PackPath);
    mstr_delete(application->MountPath);
    list_clear(&application->Commands, __FreeCommand, NULL);
    free(application);
}

static oserr_t __CreateCommandSymlink(
        _In_ struct Application* application,
        _In_ struct Command*     command)
{
    char* symlinkPath;
    char* mountPath;
    int   status;

    symlinkPath  = mstr_u8(command->SymlinkPath);
    mountPath = mstr_u8(command->MountedPath);
    if (symlinkPath == NULL || mountPath == NULL) {
        free(symlinkPath);
        free(mountPath);
        return OsOutOfMemory;
    }

    status = link(symlinkPath, mountPath, 1);
    free(symlinkPath);
    free(mountPath);

    if (status) {
        // TODO log this
        return OsInvalidParameters;
    }
    return OsOK;
}

static oserr_t __PrepareMountNamespace(
        _In_ struct Application* application)
{
    // TODO interface for filesystem scope is missing
    return OsOK;
}

oserr_t ApplicationMount(
        _In_ struct Application* application)
{
    char*   packPath;
    char*   mountPath;
    oserr_t oserr;

    if (application == NULL) {
        return OsInvalidParameters;
    }

    packPath  = mstr_u8(application->PackPath);
    mountPath = mstr_u8(application->MountPath);
    if (packPath == NULL || mountPath == NULL) {
        free(packPath);
        free(mountPath);
        return OsOutOfMemory;
    }

    // First thing we do is mount the application, and then we prepare a mount space
    // for the application.
    oserr = Mount(packPath, mountPath, "valifs", MOUNT_FLAG_READ);
    if (oserr != OsOK) {
        goto cleanup;
    }

    // Next up is creating symlinks for applications into the global mount namespace,
    // so they are visible.
    foreach(i, &application->Commands) {
        oserr = __CreateCommandSymlink(application, (struct Command*)i);
        if (oserr != OsOK) {
            // So we should not cancel everything here - instead let us log this.
            // TODO proper logging in served
        }
    }

    // Lastly, we prepare the filesystem scope for the application, which all commmands
    // for this application will be spawned under
    oserr = __PrepareMountNamespace(application);
    if (oserr != OsOK) {
        // Again log this
    }

cleanup:
    free(packPath);
    free(mountPath);
    return oserr;
}

static oserr_t __KillCommand(
        _In_ struct Application* application,
        _In_ struct Command*     command)
{

}

static oserr_t __RemoveCommandSymlink(
        _In_ struct Application* application,
        _In_ struct Command*     command)
{

}

oserr_t ApplicationUnmount(struct Application* application)
{
    char*   mountPath;
    oserr_t oserr;

    if (application == NULL) {
        return OsInvalidParameters;
    }

    mountPath = mstr_u8(application->MountPath);
    if (mountPath == NULL) {
        return OsOutOfMemory;
    }

    // Start out by going through all the commands and remove their
    // symlinks, so we don't break anything. If a command is running they
    // should also be killed here.
    foreach(i, &application->Commands) {
        struct Command* command = (struct Command*)i;

        // Kill all processes spawned by this command
        oserr = __KillCommand(application, command);
        if (oserr != OsOK) {
            // Uh this is not good, then we cannot unmount.
            free(mountPath);
            return oserr;
        }

        // Now we remove any traces of this left when we mounted it
        oserr = __RemoveCommandSymlink(application, command);
        if (oserr != OsOK) {
            // Can we live with broken symlinks?
        }
    }

    oserr = Unmount(mountPath);
    if (oserr != OsOK) {
        // Uhh log this
    }
    free(mountPath);
    return oserr;
}

static oserr_t __SpawnService(
        _In_ struct Application* application,
        _In_ struct Command*     command)
{
    // TODO implement this
    return OsNotSupported;
}

oserr_t ApplicationStartServices(struct Application* application)
{
    // Go through all commands registered to the application, if an
    // application is a service, we start it under the prepared namespace
    // for the application
    foreach(i, &application->Commands) {
        struct Command* command = (struct Command*)i;
        if (command->Type == 0) {
            oserr_t oserr = __SpawnService(application, command);
            if (oserr != OsOK) {
                // Again, continue here, but we log the error for the user
            }
        }
    }
    return OsOK;
}

oserr_t ApplicationStopServices(struct Application* application)
{

    return OsOK;
}
