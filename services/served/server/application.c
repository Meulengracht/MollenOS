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

#define __TRACE

#include <errno.h>
#include <chef/package.h>
#include <ddk/utils.h>
#include <os/services/mount.h>
#include <os/services/process.h>
#include <os/services/file.h>
#include <served/application.h>
#include <stdlib.h>
#include <stdio.h>
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

// /data/served/cache/<name>
static mstring_t* __ApplicationCachePath(struct Application* application)
{
    return mstr_fmt(
            "/data/served/cache/%ms.%ms",
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
    application->CachePath = __ApplicationCachePath(application);
    if (application->PackPath == NULL ||
        application->MountPath == NULL ||
        application->CachePath == NULL) {
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
    mstr_delete(application->CachePath);
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
    TRACE("__CreateCommandSymlink(app=%ms, cmd=%ms)", application->Name, command->Name);

    symlinkPath  = mstr_u8(command->SymlinkPath);
    mountPath = mstr_u8(command->MountedPath);
    if (symlinkPath == NULL || mountPath == NULL) {
        free(symlinkPath);
        free(mountPath);
        return OS_EOOM;
    }

    status = link(symlinkPath, mountPath, 1);
    free(symlinkPath);
    free(mountPath);

    if (status) {
        ERROR("__CreateCommandSymlink link failed with code: %i", errno);
        return OS_EINVALPARAMS;
    }
    return OS_EOK;
}

static oserr_t __PrepareMountNamespace(
        _In_ struct Application* application)
{
    // TODO interface for filesystem scope is missing
    return OS_EOK;
}

static oserr_t __CreateDirectoryIfNotExists(
        _In_ mstring_t* path)
{
    int   mode = FILE_PERMISSION_READ | FILE_PERMISSION_EXECUTE | FILE_PERMISSION_OWNER_WRITE;
    char* cpath = mstr_u8(path);
    int   status;

    if (cpath == NULL) {
        return OS_EOOM;
    }

    status = mkdir(cpath, mode);
    free(cpath);
    if (status && errno != EEXIST) {
        ERROR("__CreateDirectoryIfNotExists failed to create path %ms", path);
        return OS_EUNKNOWN;
    }
    return OS_EOK;
}

static oserr_t __EnsureApplicationRuntimePaths(
        _In_ struct Application* application)
{
    oserr_t oserr;
    TRACE("__EnsureApplicationRuntimePaths(app=%ms)", application->Name);

    // /data/served/mount/<name>
    oserr = __CreateDirectoryIfNotExists(application->MountPath);
    if (oserr != OS_EOK) {
        return oserr;
    }

    // /data/served/cache/<name>
    return __CreateDirectoryIfNotExists(application->CachePath);
}

oserr_t ApplicationMount(
        _In_ struct Application* application)
{
    char*   packPath;
    char*   mountPath;
    oserr_t oserr;
    TRACE("ApplicationMount(app=%ms)", application ? application->Name : NULL);

    if (application == NULL) {
        return OS_EINVALPARAMS;
    }

    oserr = __EnsureApplicationRuntimePaths(application);
    if (oserr != OS_EOK) {
        return oserr;
    }

    packPath  = mstr_u8(application->PackPath);
    mountPath = mstr_u8(application->MountPath);
    if (packPath == NULL || mountPath == NULL) {
        free(packPath);
        free(mountPath);
        return OS_EOOM;
    }

    // First thing we do is mount the application, and then we prepare a mount space
    // for the application.
    oserr = OSMount(packPath, mountPath, "valifs", MOUNT_FLAG_READ);
    if (oserr != OS_EOK) {
        ERROR("ApplicationMount failed to mount application %ms: %u",
              application->Name, oserr);
        goto cleanup;
    }

    // Next up is creating symlinks for applications into the global mount namespace,
    // so they are visible.
    foreach(i, &application->Commands) {
        struct Command* command = (struct Command*)i;
        oserr = __CreateCommandSymlink(application, command);
        if (oserr != OS_EOK) {
            WARNING("ApplicationMount failed to prepare command %ms", command->Name);
        }
    }

    // Lastly, we prepare the filesystem scope for the application, which all commmands
    // for this application will be spawned under
    oserr = __PrepareMountNamespace(application);
    if (oserr != OS_EOK) {
        ERROR("ApplicationMount failed to prepare mount namespace for %ms", application->Name);
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
    TRACE("__RemoveCommandSymlink(app=%ms, cmd=%ms)", application->Name, command->Name);
    // TODO not implemented yet, we need somehow track processes spawned here
    // or some other system in place
    return OS_EOK;
}

static oserr_t __RemoveCommandSymlink(
        _In_ struct Application* application,
        _In_ struct Command*     command)
{
    char* symlinkPath;
    int   status;
    TRACE("__RemoveCommandSymlink(app=%ms, cmd=%ms)", application->Name, command->Name);

    symlinkPath  = mstr_u8(command->SymlinkPath);
    if (symlinkPath == NULL) {
        free(symlinkPath);
        return OS_EOOM;
    }

    status = unlink(symlinkPath);
    free(symlinkPath);

    if (status) {
        ERROR("__RemoveCommandSymlink unlink failed with code: %i", errno);
        return OS_EINVALPARAMS;
    }
    return OS_EOK;
}

oserr_t ApplicationUnmount(struct Application* application)
{
    char*   mountPath;
    oserr_t oserr;
    TRACE("ApplicationUnmount(app=%ms)", application ? application->Name : NULL);

    if (application == NULL) {
        return OS_EINVALPARAMS;
    }

    mountPath = mstr_u8(application->MountPath);
    if (mountPath == NULL) {
        return OS_EOOM;
    }

    // Start out by going through all the commands and remove their
    // symlinks, so we don't break anything. If a command is running they
    // should also be killed here.
    foreach(i, &application->Commands) {
        struct Command* command = (struct Command*)i;

        // Kill all processes spawned by this command
        oserr = __KillCommand(application, command);
        if (oserr != OS_EOK) {
            // Uh this is not good, then we cannot unmount.
            ERROR("ApplicationUnmount failed to stop processes spawned by command %ms",
                  command->Name);
            free(mountPath);
            return oserr;
        }

        // Now we remove any traces of this left when we mounted it
        oserr = __RemoveCommandSymlink(application, command);
        if (oserr != OS_EOK) {
            // Can we live with broken symlinks?
            ERROR("ApplicationUnmount failed to remove symlinks for command %ms",
                  command->Name);
        }
    }

    oserr = OSUnmount(mountPath);
    if (oserr != OS_EOK) {
        ERROR("ApplicationUnmount failed to unmount application %ms", application->Name);
    }
    free(mountPath);
    return oserr;
}

static char* __FmtString(const char* fmt, ...)
{
    char*   buffer;
    va_list args;

    buffer = malloc(512);
    if (buffer == NULL) {
        return NULL;
    }

    va_start(args, fmt);
    snprintf(buffer, 512, fmt, args);
    va_end(args);
    return buffer;
}

static void __DestroyEnvironment(
        _In_ char** environment)
{
    if (environment == NULL) {
        return;
    }
    for (int i = 0; environment[i] != NULL; i++) {
        free(environment[i]);
    }
    free(environment);
}

static char** __BuildCommandEnvironment(
        _In_ struct Application* application,
        _In_ struct Command*     command)
{
    char** environment;

    environment = malloc(sizeof(char*) * 4);
    if (environment == NULL) {
        return NULL;
    }

    environment[0] = __FmtString("PATH=/apps;/data/bin");
    if (environment[0] == NULL) {
        __DestroyEnvironment(environment);
        return NULL;
    }
    environment[1] = __FmtString("USRDIR=/home");
    if (environment[1] == NULL) {
        __DestroyEnvironment(environment);
        return NULL;
    }
    environment[2] = __FmtString("APPDIR=%ms", application->CachePath);
    if (environment[2] == NULL) {
        __DestroyEnvironment(environment);
        return NULL;
    }
    environment[3] = NULL;
    return environment;
}

static oserr_t __SpawnService(
        _In_ struct Application* application,
        _In_ struct Command*     command)
{
    OSProcessOptions_t* procOpts;
    oserr_t             oserr;
    uuid_t              handle;
    char*               path;
    char*               args;
    char**              environment;
    TRACE("__SpawnService(app=%ms)", application->Name, command->Name);

    path = mstr_u8(command->MountedPath);
    args = mstr_u8(command->Arguments);
    if (path == NULL || args == NULL) {
        ERROR("__SpawnService path or args was null");
        free(path);
        free(args);
        return OS_EOOM;
    }

    procOpts = OSProcessOptionsNew();
    if (procOpts == NULL) {
        free(path);
        free(args);
        return OS_EOOM;
    }

    // Each application gets their own log stream setup, which will be connected
    // to each command on startup. This way it's possible to query log output for
    // each service/application seperately.
    // TODO not implemented

    // Supply each command spawned with their respective mount namespace here
    // TODO not implemented

    // Build custom, very controlled environment blocks here, derived by various
    // things but tailored to each application.
    environment = __BuildCommandEnvironment(application, command);
    if (environment == NULL) {
        ERROR("__SpawnService failed to build environment for command %ms", command->Name);
        oserr = OS_EOOM;
        goto cleanup;
    }
    OSProcessOptionsSetArguments(procOpts, args);
    OSProcessOptionsSetEnvironment(procOpts, (const char* const*)environment);

    // Spawn the process, what we should do here is to actually monitor the
    // processes spawned, so we can make sure to kill them again later. Or maybe
    // this should be tracked somewhere else
    // TODO not implemented
    oserr = OSProcessSpawnOpts(path,  procOpts, &handle);
    OSProcessOptionsDelete(procOpts);

cleanup:
    free(path);
    free(args);
    __DestroyEnvironment(environment);
    return oserr;
}

oserr_t ApplicationStartServices(struct Application* application)
{
    TRACE("ApplicationStartServices(app=%ms)", application ? application->Name : NULL);

    // Go through all commands registered to the application, if an
    // application is a service, we start it under the prepared namespace
    // for the application
    foreach(i, &application->Commands) {
        struct Command* command = (struct Command*)i;
        if (command->Type == CHEF_COMMAND_TYPE_DAEMON) {
            oserr_t oserr = __SpawnService(application, command);
            if (oserr != OS_EOK) {
                // Again, continue here, but we log the error for the user
                ERROR("ApplicationStartServices failed to spawn service %ms", command->Name);
            }
        }
    }
    return OS_EOK;
}

oserr_t ApplicationStopServices(struct Application* application)
{
    TRACE("ApplicationStopServices(app=%ms)", application ? application->Name : NULL);

    // Go through all commands registered to the application, if an
    // application is a service, we request a stop
    foreach(i, &application->Commands) {
        struct Command* command = (struct Command*)i;
        if (command->Type == CHEF_COMMAND_TYPE_DAEMON) {
            oserr_t oserr = __KillCommand(application, command);
            if (oserr != OS_EOK) {
                // Again, continue here, but we log the error for the user
                ERROR("ApplicationStopServices failed to stop service %ms", command->Name);
            }
        }
    }
    return OS_EOK;
}
