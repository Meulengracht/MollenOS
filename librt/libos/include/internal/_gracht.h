/**
 * Copyright, Philip Meulengracht
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef __OS_INTERNAL_GRACHT_H__
#define __OS_INTERNAL_GRACHT_H__

#include <gracht/client.h>
#include <os/osdefs.h>

/**
 * @brief Waits for a successfully sent request and requires a complete response.
 * Failed waits discard the local request state; remote work may still continue.
 * @param client Client that sent the request.
 * @param context Sent request context.
 * @param flags Gracht await flags.
 * @return OS_EOK when the result can be read, otherwise OS_EUNKNOWN.
 */
static inline oserr_t
__GrachtAwaitCompleted(
    _In_ gracht_client_t*               client,
    _In_ struct gracht_message_context* context,
    _In_ unsigned int                   flags)
{
    if (gracht_client_await(client, context, flags) != 0 ||
        gracht_client_get_status(client, context) != GRACHT_MESSAGE_COMPLETED) {
        gracht_client_abandon(client, context);
        return OS_EUNKNOWN;
    }
    return OS_EOK;
}

#endif //!__OS_INTERNAL_GRACHT_H__
