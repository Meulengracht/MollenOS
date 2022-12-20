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
 */
//#define __TRACE

#include <ioset.h>
#include <ddk/convert.h>
#include <gracht/link/vali.h>
#include <internal/_utils.h>
#include <os/services/process.h>

#include <ctt_filesystem_service_server.h>
#include <sys_file_service_client.h>

extern gracht_server_t* __crt_get_module_server(void);
extern int __crt_get_server_iod(void);

oserr_t
OnLoad(void)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetFileService());

    // register the filesystem protocol so we can handle the filesystem requests
    gracht_server_register_protocol(__crt_get_module_server(), &ctt_filesystem_server_protocol);

    // notify the file manager that we are now ready for requests
    sys_file_fsready(
            GetGrachtClient(),
            &msg.base,
            OSProcessCurrentID(),
            GetNativeHandle(__crt_get_server_iod())
    );
    return OS_EOK;
}

void
OnUnload(void)
{
    // no unload actions
}

oserr_t OnEvent(struct ioset_event* event)
{
    // we don't use interrupts in filesystem drivers
    return OS_ENOENT;
}

// again lazyness in libddk causing this
void sys_device_event_protocol_device_invocation(void) {

}
void sys_device_event_device_update_invocation(void) {

}
