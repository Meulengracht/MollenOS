#ifndef __INTERNAL_IPC_H__
#define __INTERNAL_IPC_H__

#include <ds/streambuffer.h>
#include <ddk/protocols/svc_file_protocol_client.h>
#include <ddk/protocols/svc_path_protocol_client.h>
#include <ddk/protocols/svc_library_protocol_client.h>
#include <ddk/protocols/svc_process_protocol_client.h>
#include <ddk/protocols/svc_socket_protocol_client.h>
#include <ddk/protocols/svc_storage_protocol_client.h>
#include <ddk/service.h>
#include <gracht/link/vali.h>
#include <internal/_utils.h>
#include <os/dmabuf.h>

struct ipcontext {
    streambuffer_t* stream;
};

#endif //!__INTERNAL_IPC_H__
