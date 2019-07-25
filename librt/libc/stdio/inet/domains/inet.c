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
 * Standard C Support
 * - Standard Socket IO Implementation
 */

#include <internal/_io.h>

static intmax_t inet_recv(stdio_handle_t* handle, struct msghdr* msg, int flags)
{
    return 0;
}

static intmax_t inet_send(stdio_handle_t* handle, const struct msghdr* msg, int flags)
{
    return 0;
}

void get_socket_ops_inet(struct socket_ops* ops)
{
    ops->recv = inet_recv;
    ops->send = inet_send;
}
