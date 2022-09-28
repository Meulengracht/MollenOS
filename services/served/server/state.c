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

#include <cjson.h>
#include <os/usched/mutex.h>
#include <served/state.h>
#include <stdio.h>
#include <stdlib.h>

static struct State g_globalState = { 0 };
struct usched_mtx   g_stateMutex  = { 0 };

static oserr_t __ReadState(const char* path, void** bufferOut, size_t* bufferSize)
{
    FILE*   stateFile = fopen(path, "r");
    oserr_t oserr     = OsOK;
    if (stateFile == NULL) {
        return OsNotExists;
    }

    fseek(stateFile, 0, SEEK_END);
    long size = ftell(stateFile);
    if (size == 0) {
        oserr = OsIncomplete;
        goto exit;
    }

    void* data = malloc(size);
    if (data == NULL) {
        oserr = OsOutOfMemory;
        goto exit;
    }

    size_t read = fread(data, 1, size, stateFile);
    if (read != (size_t)size) {
        oserr = OsIncomplete;
        goto exit;
    }

    *bufferOut = data;
    *bufferSize = read;

exit:
    fclose(stateFile);
    return oserr;
}

static struct Command* __CommandFromJson(
        const cJSON* name,
        const cJSON* path,
        const cJSON* arguments,
        const cJSON* type)
{
    struct Command* command;

    if (name == NULL || path == NULL || type == NULL) {
        return NULL;
    }

    command = malloc(sizeof(struct Command));
    if (command == NULL) {
        return NULL;
    }

    ELEMENT_INIT(&command->ListHeader, 0, 0);
    command->Name = mstr_new_u8(cJSON_GetStringValue(name));
    command->Path = mstr_new_u8(cJSON_GetStringValue(path));
    command->Arguments = mstr_new_u8(cJSON_GetStringValue(arguments));
    command->Type = (int)cJSON_GetNumberValue(type);
    return command;
}

static oserr_t __ParseCommands(list_t* out, cJSON* in)
{
    int cmdCount = cJSON_GetArraySize(in);
    for (int i = 0; i < cmdCount; i++) {
        cJSON* cmdObject = cJSON_GetArrayItem(in, i);
        if (cmdObject == NULL) {
            return OsError;
        }

        // Get command members
        cJSON* nameObject = cJSON_GetObjectItem(cmdObject, "name");
        cJSON* pathObject = cJSON_GetObjectItem(cmdObject, "path");
        cJSON* argumentsObject = cJSON_GetObjectItem(cmdObject, "arguments");
        cJSON* typeObject = cJSON_GetObjectItem(cmdObject, "type");
        struct Command* command = __CommandFromJson(
                nameObject, pathObject, argumentsObject,typeObject);
        if (command == NULL) {
            return OsError;
        }
        list_append(out, &command->ListHeader);
    }
    return OsOK;
}

static struct Application* __ApplicationFromJson(
        const cJSON* name,
        const cJSON* publisher,
        const cJSON* package,
        const cJSON* major,
        const cJSON* minor,
        const cJSON* patch,
        const cJSON* revision)
{
    struct Application* application;

    if (name == NULL || publisher == NULL || package == NULL) {
        return NULL;
    }

    application = malloc(sizeof(struct Application));
    if (application == NULL) {
        return NULL;
    }

    ELEMENT_INIT(&application->ListHeader, 0, 0);
    application->Name = mstr_new_u8(cJSON_GetStringValue(name));
    application->Publisher = mstr_new_u8(cJSON_GetStringValue(publisher));
    application->Package = mstr_new_u8(cJSON_GetStringValue(package));
    application->Major = (int)cJSON_GetNumberValue(major);
    application->Minor = (int)cJSON_GetNumberValue(minor);
    application->Patch = (int)cJSON_GetNumberValue(patch);
    application->Revision = (int)cJSON_GetNumberValue(revision);
    list_construct(&application->Commands);
    return application;
}

static oserr_t __ParseApplications(list_t* out, const cJSON* in)
{
    int appCount = cJSON_GetArraySize(in);
    for (int i = 0; i < appCount; i++) {
        cJSON* appObject = cJSON_GetArrayItem(in, i);
        if (appObject == NULL) {
            return OsError;
        }

        // Get application members
        cJSON* nameObject = cJSON_GetObjectItem(appObject, "name");
        cJSON* publisherObject = cJSON_GetObjectItem(appObject, "publisher");
        cJSON* packageObject = cJSON_GetObjectItem(appObject, "package");
        cJSON* majorObject = cJSON_GetObjectItem(appObject, "major");
        cJSON* minorObject = cJSON_GetObjectItem(appObject, "minor");
        cJSON* patchObject = cJSON_GetObjectItem(appObject, "patch");
        cJSON* revisionObject = cJSON_GetObjectItem(appObject, "revision");
        struct Application* application = __ApplicationFromJson(
                nameObject, publisherObject, packageObject,
                majorObject, minorObject,
                patchObject, revisionObject);
        if (application == NULL) {
            return OsError;
        }

        // Get application members
        cJSON* commands = cJSON_GetObjectItem(appObject, "commands");
        if (commands != NULL) {
            oserr_t oserr = __ParseCommands(&application->Commands, commands);
            if (oserr != OsOK) {
                return oserr;
            }
        }
        list_append(out, &application->ListHeader);
    }
    return OsOK;
}

oserr_t __ParseState(struct State* state, const void* buffer, size_t bufferLength)
{
    cJSON* root = cJSON_ParseWithLength(buffer, bufferLength);
    if (root == NULL) {
        return OsInvalidParameters;
    }

    cJSON* firstBoot = cJSON_GetObjectItem(root, "first-boot");
    state->FirstBoot = cJSON_IsTrue(firstBoot);

    cJSON* applications = cJSON_GetObjectItem(root, "applications");
    if (applications != NULL) {
        oserr_t oserr = __ParseApplications(&state->Applications, applications);
        if (oserr != OsOK) {
            cJSON_Delete(root);
            return oserr;
        }
    }

    cJSON_Delete(root);
    return OsOK;
}

static void __StateConstruct(struct State* state)
{
    state->FirstBoot = true;
    list_construct(&state->Applications);
}

oserr_t StateLoad(void)
{
    void*   stateData;
    size_t  stateDataLength;
    oserr_t oserr;

    // initialize globals
    usched_mtx_init(&g_stateMutex);
    __StateConstruct(&g_globalState);

    oserr = __ReadState("/data/served/state.json", &stateData, &stateDataLength);
    if (oserr != OsOK) {
        // Use a new state
        return OsOK;
    }

    oserr = __ParseState(&g_globalState, stateData, stateDataLength);
    free(stateData);
    return oserr;
}

struct State* State(void) {
    return &g_globalState;
}

void StateLock(void) {
    usched_mtx_lock(&g_stateMutex);
}

void StateUnlock(void) {
    usched_mtx_unlock(&g_stateMutex);
}
