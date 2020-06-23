#ifndef __INTERNAL_IPC_H__
#define __INTERNAL_IPC_H__

#include <ds/streambuffer.h>

#include <svc_device_protocol_client.h>
#include <svc_file_protocol_client.h>
#include <svc_path_protocol_client.h>
#include <svc_library_protocol_client.h>
#include <svc_process_protocol_client.h>
#include <svc_session_protocol_client.h>
#include <svc_socket_protocol_client.h>
#include <svc_storage_protocol_client.h>
#include <svc_usb_protocol_client.h>

#include <ctt_driver_protocol_client.h>
#include <ctt_storage_protocol_client.h>
#include <ctt_usbhost_protocol_client.h>

#include <ddk/service.h>
#include <gracht/link/vali.h>
#include <internal/_utils.h>
#include <os/dmabuf.h>

struct ipcontext {
    streambuffer_t* stream;
};

#endif //!__INTERNAL_IPC_H__
