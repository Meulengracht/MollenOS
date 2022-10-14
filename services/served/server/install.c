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

#include <chef/package.h>
#include <ddk/utils.h>
#include <ds/mstring.h>
#include <io.h>
#include <stdlib.h>
#include <served/application.h>
#include <served/state.h>
#include <served/utils.h>
#include <string.h>

// Packs are installed in a very predictable format.
// <publisher>.<package>.pack
static oserr_t __GetPublisher(const char* basename, mstring_t** publisherOut)
{
    char* seperator = strchr(basename, '.');
    if (seperator == NULL) {
        return OsInvalidParameters;
    }

    char* token = strndup(basename, seperator - basename);
    if (token == NULL) {
        return OsOutOfMemory;
    }

    *publisherOut = mstr_new_u8(token);
    free(token);
    return *publisherOut == NULL ? OsOutOfMemory : OsOK;
}

static oserr_t __ParsePackage(mstring_t* publisher, mstring_t* path, struct Application** applicationOut)
{
    struct Application*  application;
    struct chef_package* package;
    struct chef_version* version;
    struct chef_command* commands;
    int                  count;
    int                  status;
    oserr_t              oserr = OsOK;
    char*                pathu8;
    TRACE("__ParsePackage(publisher=%ms, path=%ms)", publisher, path);

    pathu8 = mstr_u8(path);
    if (pathu8 == NULL) {
        return OsOutOfMemory;
    }

    status = chef_package_load(
            pathu8,
            &package,
            &version,
            &commands,
            &count
    );
    free(pathu8);
    if (status) {
        return OsError;
    }

    application = ApplicationNew(
            mstr_fmt("%ms/%s", publisher, package->package),
            publisher,
            mstr_new_u8(package->package),
            version->major,
            version->minor,
            version->patch,
            version->revision
    );
    if (application == NULL) {
        oserr = OsError;
        goto cleanup;
    }

    // add parsed commands
    for (int i = 0; i < count; i++) {
        struct Command* command = CommandNew(
                application,
                mstr_new_u8(commands[i].name),
                mstr_new_u8(commands[i].path),
                mstr_new_u8(commands[i].arguments),
                (int)commands[i].type
        );
        if (command == NULL) {
            oserr = OsOutOfMemory;
            goto cleanup;
        }
    }

    *applicationOut = application;

cleanup:
    chef_package_free(package);
    chef_version_free(version);
    chef_commands_free(commands, count);
    if (oserr != OsOK) {
        ApplicationDelete(application);
    }
    return oserr;
}

static oserr_t __InstallPack(mstring_t* path, struct Application* application)
{
    char*   sourceu8;
    char*   destinationu8;
    oserr_t oserr;
    TRACE("__InstallPack(path=%ms)", path);

    sourceu8 = mstr_u8(path);
    destinationu8 = mstr_u8(application->PackPath);
    if (sourceu8 == NULL || destinationu8 == NULL) {
        free(sourceu8);
        free(destinationu8);
        return OsOutOfMemory;
    }

    oserr = CopyFile(sourceu8, destinationu8);
    free(sourceu8);
    free(destinationu8);
    return oserr;
}

static oserr_t __RegisterApplication(struct Application* application)
{
    struct State* state = State();
    TRACE("__RegisterApplication(app=%ms)", application->Name);

    StateLock();
    // we should definitely do a check here that we are not double installing
    // something we shouldn't
    foreach(i, &state->Applications) {
        struct Application* a = (struct Application*)i;
        // TODO do some kind of security check here
        if (!mstr_cmp(a->Name, application->Name)) {
            StateUnlock();
            return OsExists;
        }
    }

    list_append(&state->Applications, &application->ListHeader);
    StateUnlock();
    return OsOK;
}

oserr_t InstallApplication(mstring_t* path, const char* basename)
{
    struct Application* application;
    mstring_t*          publisher;
    oserr_t             oserr;
    TRACE("InstallApplication(path=%ms, basename=%s)", path, basename);

    oserr = __GetPublisher(basename, &publisher);
    if (oserr != OsOK) {
        return oserr;
    }

    oserr = __ParsePackage(publisher, path, &application);
    if (oserr != OsOK) {
        return oserr;
    }

    oserr = __InstallPack(path, application);
    if (oserr != OsOK) {
        return oserr;
    }

    oserr = __RegisterApplication(application);
    if (oserr != OsOK) {
        return oserr;
    }

    oserr = ApplicationMount(application);
    if (oserr != OsOK) {
        return oserr;
    }
    return ApplicationStartServices(application);
}

void InstallBundledApplications(void)
{
    struct DIR*    setupDir;
    struct dirent* entry;
    TRACE("InstallBundledApplications()");

    setupDir = opendir("/data/setup");
    if (setupDir == NULL) {
        // directory did not exist, no bundled apps to install
        return;
    }

    while ((entry = readdir(setupDir)) != NULL) {
        mstring_t* path = mstr_fmt("/data/setup/%s", &entry->d_name[0]);
        oserr_t oserr = InstallApplication(path, &entry->d_name[0]);
        if (oserr != OsOK) {
            // Not a compatable file, delete it
            char* pathu8 = mstr_u8(path);
            if (unlink(pathu8)) {
                ERROR("InstallBundledApplications failed to unlink path %s", pathu8);
            }
            free(pathu8);
        }
        mstr_delete(path);
    }

    // finally remove the directory as installation has completed.
    (void)closedir(setupDir);
    if (unlink("/data/setup")) {
        ERROR("InstallBundledApplications failed to remove /data/setup");
    }
}
