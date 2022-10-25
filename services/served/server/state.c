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
#include <string.h>

static struct State g_globalState = { 0 };
struct usched_mtx   g_stateMutex  = { 0 };

static oserr_t __ReadState(const char* path, void** bufferOut, size_t* bufferSize)
{
    FILE*   stateFile = fopen(path, "r");
    oserr_t oserr     = OS_EOK;
    if (stateFile == NULL) {
        return OS_ENOENT;
    }

    fseek(stateFile, 0, SEEK_END);
    long size = ftell(stateFile);
    if (size == 0) {
        oserr = OS_EINCOMPLETE;
        goto exit;
    }

    void* data = malloc(size);
    if (data == NULL) {
        oserr = OS_EOOM;
        goto exit;
    }

    size_t read = fread(data, 1, size, stateFile);
    if (read != (size_t)size) {
        oserr = OS_EINCOMPLETE;
        goto exit;
    }

    *bufferOut = data;
    *bufferSize = read;

exit:
    fclose(stateFile);
    return oserr;
}

static struct Command* __CommandFromJson(
        struct Application* application,
        const cJSON* name,
        const cJSON* path,
        const cJSON* arguments,
        const cJSON* type)
{
    if (name == NULL || path == NULL || type == NULL) {
        return NULL;
    }

    return CommandNew(
            application,
            mstr_new_u8(cJSON_GetStringValue(name)),
            mstr_new_u8(cJSON_GetStringValue(path)),
            mstr_new_u8(cJSON_GetStringValue(arguments)),
            (int)cJSON_GetNumberValue(type)
    );
}

static oserr_t __ParseCommands(struct Application* application, cJSON* in)
{
    int cmdCount = cJSON_GetArraySize(in);
    for (int i = 0; i < cmdCount; i++) {
        cJSON* cmdObject = cJSON_GetArrayItem(in, i);
        if (cmdObject == NULL) {
            return OS_EUNKNOWN;
        }

        // Get command members
        cJSON* nameObject = cJSON_GetObjectItem(cmdObject, "name");
        cJSON* pathObject = cJSON_GetObjectItem(cmdObject, "path");
        cJSON* argumentsObject = cJSON_GetObjectItem(cmdObject, "arguments");
        cJSON* typeObject = cJSON_GetObjectItem(cmdObject, "type");
        struct Command* command = __CommandFromJson(application,
                nameObject, pathObject,
                argumentsObject, typeObject
        );
        if (command == NULL) {
            return OS_EUNKNOWN;
        }
    }
    return OS_EOK;
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
    if (name == NULL || publisher == NULL || package == NULL) {
        return NULL;
    }

    return ApplicationNew(
            mstr_new_u8(cJSON_GetStringValue(name)),
            mstr_new_u8(cJSON_GetStringValue(publisher)),
            mstr_new_u8(cJSON_GetStringValue(package)),
            (int)cJSON_GetNumberValue(major),
            (int)cJSON_GetNumberValue(minor),
            (int)cJSON_GetNumberValue(patch),
            (int)cJSON_GetNumberValue(revision)
    );
}

static oserr_t __ParseApplications(list_t* out, const cJSON* in)
{
    int appCount = cJSON_GetArraySize(in);
    for (int i = 0; i < appCount; i++) {
        cJSON* appObject = cJSON_GetArrayItem(in, i);
        if (appObject == NULL) {
            return OS_EUNKNOWN;
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
            return OS_EUNKNOWN;
        }

        // Get application members
        cJSON* commands = cJSON_GetObjectItem(appObject, "commands");
        if (commands != NULL) {
            oserr_t oserr = __ParseCommands(application, commands);
            if (oserr != OS_EOK) {
                return oserr;
            }
        }
        list_append(out, &application->ListHeader);
    }
    return OS_EOK;
}

oserr_t __ParseState(struct State* state, const void* buffer, size_t bufferLength)
{
    cJSON* root = cJSON_ParseWithLength(buffer, bufferLength);
    if (root == NULL) {
        return OS_EINVALPARAMS;
    }

    cJSON* firstBoot = cJSON_GetObjectItem(root, "first-boot");
    state->FirstBoot = cJSON_IsTrue(firstBoot);

    cJSON* applications = cJSON_GetObjectItem(root, "applications");
    if (applications != NULL) {
        oserr_t oserr = __ParseApplications(&state->Applications, applications);
        if (oserr != OS_EOK) {
            cJSON_Delete(root);
            return oserr;
        }
    }

    cJSON_Delete(root);
    return OS_EOK;
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
    if (oserr != OS_EOK) {
        // Use a new state
        return OS_EOK;
    }

    oserr = __ParseState(&g_globalState, stateData, stateDataLength);
    free(stateData);
    return oserr;
}

static void __AddStringToObject(cJSON* out, const char* key, mstring_t* in)
{
    char* stru8 = mstr_u8(in);
    cJSON_AddStringToObject(out, key, stru8);
    free(stru8);
}

static oserr_t __SerializeCommand(cJSON** out, struct Command* in)
{
    cJSON* command = cJSON_CreateObject();
    if (command == NULL) {
        return OS_EOOM;
    }

    __AddStringToObject(command, "name", in->Name);
    __AddStringToObject(command, "path", in->Path);
    __AddStringToObject(command, "arguments", in->Arguments);
    cJSON_AddNumberToObject(command, "type", in->Type);

    *out = command;
    return OS_EOK;
}

static oserr_t __SerializeCommands(cJSON* out, list_t* in)
{
    foreach(i, in) {
        cJSON*  command;
        oserr_t oserr;

        oserr = __SerializeCommand(&command, (struct Command*)i);
        if (oserr != OS_EOK) {
            return oserr;
        }

        if (cJSON_AddItemToArray(out, command)) {
            return OS_EOOM;
        }
    }
    return OS_EOK;
}

static oserr_t __SerializeApplication(cJSON** out, struct Application* in)
{
    cJSON* application = cJSON_CreateObject();
    if (application == NULL) {
        return OS_EOOM;
    }

    __AddStringToObject(application, "name", in->Name);
    __AddStringToObject(application, "publisher", in->Publisher);
    __AddStringToObject(application, "package", in->Package);
    cJSON_AddNumberToObject(application, "major", in->Major);
    cJSON_AddNumberToObject(application, "minor", in->Minor);
    cJSON_AddNumberToObject(application, "patch", in->Patch);
    cJSON_AddNumberToObject(application, "revision", in->Revision);

    oserr_t oserr = __SerializeCommands(cJSON_AddArrayToObject(application, "commands"), &in->Commands);
    if (oserr != OS_EOK) {
        cJSON_Delete(application);
        return oserr;
    }

    *out = application;
    return OS_EOK;
}

static oserr_t __SerializeApplications(cJSON* out, list_t* in)
{
    foreach(i, in) {
        cJSON*  application;
        oserr_t oserr;

        oserr = __SerializeApplication(&application, (struct Application*)i);
        if (oserr != OS_EOK) {
            return oserr;
        }

        if (cJSON_AddItemToArray(out, application)) {
            return OS_EOOM;
        }
    }
    return OS_EOK;
}

static oserr_t __SerializeState(struct State* state, cJSON** rootOut)
{
    cJSON*  root = cJSON_CreateObject();
    oserr_t oserr;
    if (root == NULL) {
        return OS_EOOM;
    }

    cJSON_AddBoolToObject(root, "first-boot", state->FirstBoot ? cJSON_True : cJSON_False);
    oserr = __SerializeApplications(cJSON_AddArrayToObject(root, "applications"), &state->Applications);
    if (oserr != OS_EOK) {
        cJSON_Delete(root);
        return oserr;
    }

    *rootOut = root;
    return OS_EOK;
}

static oserr_t __WriteState(const char* path, const char* buffer)
{
    FILE*   stateFile = fopen(path, "w+");
    oserr_t oserr     = OS_EOK;
    if (stateFile == NULL) {
        return OS_EINVALPARAMS;
    }

    size_t length = strlen(buffer);
    size_t written = fwrite(buffer, 1, length, stateFile);
    if (written != length) {
        oserr = OS_EINCOMPLETE;
    }

    fclose(stateFile);
    return oserr;
}

oserr_t StateSave(void)
{
    cJSON*  root;
    oserr_t oserr;

    oserr = __SerializeState(&g_globalState, &root);
    if (oserr != OS_EOK) {
        return oserr;
    }

    char* data = cJSON_Print(root);
    cJSON_Delete(root);
    if (data == NULL) {
        return OS_EOOM;
    }

    oserr = __WriteState("/data/served/state.json", data);
    free(data);
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
