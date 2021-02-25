/**
 * MollenOS
 *
 * Copyright 2011, Philip Meulengracht
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
 * MollenOS - File Manager Service
 * - Handles all file related services and disk services
 */
//#define __TRACE

#include <ctype.h>
#include <ddk/utils.h>
#include <ds/list.h>
#include "include/vfs.h"
#include <internal/_ipc.h>

#include <svc_file_protocol_server.h>

extern void svc_file_open_callback(struct gracht_recv_message* message, struct svc_file_open_args*);
extern void svc_file_close_callback(struct gracht_recv_message* message, struct svc_file_close_args*);
extern void svc_file_delete_callback(struct gracht_recv_message* message, struct svc_file_delete_args*);
extern void svc_file_transfer_async_callback(struct gracht_recv_message* message, struct svc_file_transfer_async_args*);
extern void svc_file_transfer_callback(struct gracht_recv_message* message, struct svc_file_transfer_args*);
extern void svc_file_transfer_absolute_callback(struct gracht_recv_message* message, struct svc_file_transfer_absolute_args*);
extern void svc_file_seek_callback(struct gracht_recv_message* message, struct svc_file_seek_args*);
extern void svc_file_flush_callback(struct gracht_recv_message* message, struct svc_file_flush_args*);
extern void svc_file_move_callback(struct gracht_recv_message* message, struct svc_file_move_args*);
extern void svc_file_set_options_callback(struct gracht_recv_message* message, struct svc_file_set_options_args*);
extern void svc_file_get_position_callback(struct gracht_recv_message* message, struct svc_file_get_position_args*);
extern void svc_file_get_options_callback(struct gracht_recv_message* message, struct svc_file_get_options_args*);
extern void svc_file_get_size_callback(struct gracht_recv_message* message, struct svc_file_get_size_args*);
extern void svc_file_get_path_callback(struct gracht_recv_message* message, struct svc_file_get_path_args*);
extern void svc_file_fstat_callback(struct gracht_recv_message* message, struct svc_file_fstat_args*);
extern void svc_file_fsstat_callback(struct gracht_recv_message* message, struct svc_file_fsstat_args*);
extern void svc_file_fstat_from_path_callback(struct gracht_recv_message* message, struct svc_file_fstat_from_path_args*);
extern void svc_file_fsstat_from_path_callback(struct gracht_recv_message* message, struct svc_file_fsstat_from_path_args*);

static gracht_protocol_function_t svc_file_callbacks[18] = {
    { PROTOCOL_SVC_FILE_OPEN_ID , svc_file_open_callback },
    { PROTOCOL_SVC_FILE_CLOSE_ID , svc_file_close_callback },
    { PROTOCOL_SVC_FILE_DELETE_ID , svc_file_delete_callback },
    { PROTOCOL_SVC_FILE_TRANSFER_ASYNC_ID , svc_file_transfer_async_callback },
    { PROTOCOL_SVC_FILE_TRANSFER_ID , svc_file_transfer_callback },
    { PROTOCOL_SVC_FILE_TRANSFER_ABSOLUTE_ID, svc_file_transfer_absolute_callback },
    { PROTOCOL_SVC_FILE_SEEK_ID , svc_file_seek_callback },
    { PROTOCOL_SVC_FILE_FLUSH_ID , svc_file_flush_callback },
    { PROTOCOL_SVC_FILE_MOVE_ID , svc_file_move_callback },
    { PROTOCOL_SVC_FILE_SET_OPTIONS_ID , svc_file_set_options_callback },
    { PROTOCOL_SVC_FILE_GET_POSITION_ID , svc_file_get_position_callback },
    { PROTOCOL_SVC_FILE_GET_OPTIONS_ID , svc_file_get_options_callback },
    { PROTOCOL_SVC_FILE_GET_SIZE_ID , svc_file_get_size_callback },
    { PROTOCOL_SVC_FILE_GET_PATH_ID , svc_file_get_path_callback },
    { PROTOCOL_SVC_FILE_FSTAT_ID , svc_file_fstat_callback },
    { PROTOCOL_SVC_FILE_FSSTAT_ID , svc_file_fsstat_callback },
    { PROTOCOL_SVC_FILE_FSTAT_FROM_PATH_ID , svc_file_fstat_from_path_callback },
    { PROTOCOL_SVC_FILE_FSSTAT_FROM_PATH_ID , svc_file_fsstat_from_path_callback },
};
DEFINE_SVC_FILE_SERVER_PROTOCOL(svc_file_callbacks, 18);

#include <svc_path_protocol_server.h>

extern void svc_path_resolve_callback(struct gracht_recv_message* message, struct svc_path_resolve_args*);
extern void svc_path_canonicalize_callback(struct gracht_recv_message* message, struct svc_path_canonicalize_args*);

static gracht_protocol_function_t svc_path_callbacks[2] = {
    { PROTOCOL_SVC_PATH_RESOLVE_ID , svc_path_resolve_callback },
    { PROTOCOL_SVC_PATH_CANONICALIZE_ID , svc_path_canonicalize_callback },
};
DEFINE_SVC_PATH_SERVER_PROTOCOL(svc_path_callbacks, 2);

#include <svc_storage_protocol_server.h>

extern void svc_storage_register_callback(struct gracht_recv_message* message, struct svc_storage_register_args*);
extern void svc_storage_unregister_callback(struct gracht_recv_message* message, struct svc_storage_unregister_args*);
extern void svc_storage_get_descriptor_callback(struct gracht_recv_message* message, struct svc_storage_get_descriptor_args*);
extern void svc_storage_get_descriptor_from_path_callback(struct gracht_recv_message* message, struct svc_storage_get_descriptor_from_path_args*);

static gracht_protocol_function_t svc_storage_callbacks[4] = {
    { PROTOCOL_SVC_STORAGE_REGISTER_ID , svc_storage_register_callback },
    { PROTOCOL_SVC_STORAGE_UNREGISTER_ID , svc_storage_unregister_callback },
    { PROTOCOL_SVC_STORAGE_GET_DESCRIPTOR_ID , svc_storage_get_descriptor_callback },
    { PROTOCOL_SVC_STORAGE_GET_DESCRIPTOR_FROM_PATH_ID , svc_storage_get_descriptor_from_path_callback },
};
DEFINE_SVC_STORAGE_SERVER_PROTOCOL(svc_storage_callbacks, 4);

static list_t g_fileSystems = LIST_INIT;

list_t* VfsGetFileSystems(void) {
    return &g_fileSystems;
}

OsStatus_t OnUnload(void)
{
    return OsSuccess;
}

void GetServiceAddress(struct ipmsg_addr* address)
{
    address->type = IPMSG_ADDRESS_PATH;
    address->data.path = SERVICE_FILE_PATH;
}

OsStatus_t OnLoad(void)
{
    // Initialize subsystems
    VfsCacheInitialize();

    // Register supported interfaces
    gracht_server_register_protocol(&svc_file_server_protocol);
    gracht_server_register_protocol(&svc_path_server_protocol);
    gracht_server_register_protocol(&svc_storage_server_protocol);
    return OsSuccess;
}
