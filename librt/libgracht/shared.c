/**
 * MollenOS
 *
 * Copyright 2019, Philip Meulengracht
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
 * Gracht Client Type Definitions & Structures
 * - This header describes the base client-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include "include/gracht/types.h"
#include "include/gracht/list.h"
#include "include/gracht/debug.h"
#include <errno.h>

// client callbacks
typedef void (*client_invoke00_t)(void);
typedef void (*client_invokeA0_t)(void*);

// server callbacks
typedef void (*server_invoke00_t)(struct gracht_recv_message*);
typedef void (*server_invokeA0_t)(struct gracht_recv_message*, void*);

static gracht_protocol_function_t* get_protocol_action(struct gracht_list* protocols,
    uint8_t protocol_id, uint8_t action_id)
{
    int                i;
    gracht_protocol_t* protocol = (struct gracht_protocol*)
        gracht_list_lookup(protocols, (int)(uint32_t)protocol_id);
    
    if (!protocol) {
        ERROR("[get_protocol_action] protocol %u was not implemented", protocol_id);
        errno = ENOTSUP;
        return NULL;
    }
    
    for (i = 0; i < protocol->num_functions; i++) {
        if (protocol->functions[i].id == action_id) {
            return &protocol->functions[i];
        }
    }

    ERROR("[get_protocol_action] action %u was not implemented", action_id);
    errno = ENOTSUP;
    return NULL;
}

static void unpack_parameters(struct gracht_param* params, uint8_t count, void* params_storage, uint8_t* unpackBuffer)
{
    char* pointer = params_storage;
    int   unpackIndex    = 0;
    int   i;

    for (i = 0; i < count; i++) {
        if (params[i].type == GRACHT_PARAM_VALUE) {
            TRACE("push value: %u\n", (uint32_t)(params[i].data.value & 0xFFFFFFFF));
            if (params[i].length == 1) {
                unpackBuffer[unpackIndex] = (uint8_t)(params[i].data.value & 0xFF);
            }
            else if (params[i].length == 2) {
                *((uint16_t*)&unpackBuffer[unpackIndex]) = (uint16_t)(params[i].data.value & 0xFFFF);
            }
            else if (params[i].length == 4) {
                *((uint32_t*)&unpackBuffer[unpackIndex]) = (uint32_t)(params[i].data.value & 0xFFFFFFFF);
            }
#if defined(amd64) || defined(__amd64__)
            else if (params[i].length == 8) {
                *((uint64_t*)&unpackBuffer[unpackIndex]) = params[i].data.value;
            }
#endif
            unpackIndex += params[i].length;
        }
        else if (params[i].type == GRACHT_PARAM_BUFFER) {
            TRACE("push pointer: 0x%p %u\n", pointer, params[i].length);
            if (params[i].length == 0) {
                *((char**)&unpackBuffer[unpackIndex]) = NULL;
            }
            else {
                *((char**)&unpackBuffer[unpackIndex]) = pointer;
                pointer += params[i].length;
            }
            unpackIndex += sizeof(void*);
        }
        else if (params[i].type == GRACHT_PARAM_SHM) {
            *((char**)&unpackBuffer[unpackIndex]) = (char*)params[i].data.buffer;
            unpackIndex += sizeof(void*);
        }
    }
}

int server_invoke_action(struct gracht_list* protocols, struct gracht_recv_message* recvMessage)
{
    gracht_protocol_function_t* function = get_protocol_action(protocols,
        recvMessage->protocol, recvMessage->action);
    void* param_storage;
    
    if (!function) {
        return -1;
    }
    
    param_storage = ((char*)recvMessage->params +
        (recvMessage->param_count * sizeof(struct gracht_param)));
    
    TRACE("offset: %lu, param count %i\n",
        recvMessage->param_count * sizeof(struct gracht_param), recvMessage->param_count);
    if (recvMessage->param_in) {
        uint8_t unpackBuffer[recvMessage->param_in * sizeof(void*)];
        unpack_parameters(recvMessage->params, recvMessage->param_in, param_storage, &unpackBuffer[0]);
        ((server_invokeA0_t)function->address)(recvMessage, &unpackBuffer[0]);
    }
    else {
        ((server_invoke00_t)function->address)(recvMessage);
    }
    return 0;
}

int client_invoke_action(struct gracht_list* protocols, struct gracht_message* message)
{
    gracht_protocol_function_t* function = get_protocol_action(protocols,
        message->header.protocol, message->header.action);
    uint32_t param_count;
    void*    param_storage;

    if (!function) {
        return -1;
    }
    
    param_count   = message->header.param_in + message->header.param_out;
    param_storage = (char*)message + sizeof(struct gracht_message) +
            (message->header.param_in * sizeof(struct gracht_param));
    
    // parse parameters into a parameter struct
    TRACE("offset: %lu, param count %i\n", param_count * sizeof(struct gracht_param), param_count);
    if (param_count) {
        uint8_t unpackBuffer[param_count * sizeof(void*)];
        unpack_parameters(&message->params[0], message->header.param_in, param_storage, &unpackBuffer[0]);
        ((client_invokeA0_t)function->address)(&unpackBuffer[0]);
    }
    else {
        ((client_invoke00_t)function->address)();
    }
    return 0;
}
