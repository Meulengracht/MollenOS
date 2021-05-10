#ifndef __INTERNAL_IPC_H__
#define __INTERNAL_IPC_H__

#include <ds/streambuffer.h>

#include <sys_device_service_client.h>
#include <sys_file_service_client.h>
#include <sys_path_service_client.h>
#include <sys_library_service_client.h>
#include <sys_process_service_client.h>
#include <sys_session_service_client.h>
#include <sys_socket_service_client.h>
#include <sys_storage_service_client.h>
#include <sys_usb_service_client.h>

#include <ctt_driver_service_client.h>
#include <ctt_storage_service_client.h>
#include <ctt_usbhost_service_client.h>
#include <ctt_usbhub_service_client.h>

#include <ddk/service.h>
#include <gracht/link/vali.h>
#include <internal/_utils.h>
#include <os/dmabuf.h>

struct ipcontext {
    streambuffer_t* stream;
    unsigned int    options;
};

#endif //!__INTERNAL_IPC_H__
