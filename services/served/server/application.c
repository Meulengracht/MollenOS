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

#include <served/application.h>
#include <stdlib.h>

struct Command*
CommandNew(
        _In_ mstring_t* name,
        _In_ mstring_t* path,
        _In_ mstring_t* arguments,
        _In_ int        type)
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
    list_clear(&application->Commands, __FreeCommand, NULL);
    free(application);
}

oserr_t ApplicationMount(struct Application* application)
{

}

oserr_t ApplicationUnmount(struct Application* application)
{

}

oserr_t ApplicationStartServices(struct Application* application)
{

}

oserr_t ApplicationStopServices(struct Application* application)
{

}

// /data/served/apps/<publisher>.<package>.pack
mstring_t* ApplicationPackPath(struct Application* application)
{
    return mstr_fmt(
            "/data/served/apps/%ms.%ms.pack",
            application->Publisher,
            application->Package
    );
}
