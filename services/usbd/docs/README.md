# USB service architecture

usbd tracks USB controllers, hubs and attached devices. It enumerates new devices,
registers them with the device service, and coordinates removal. A single topology
worker processes topology commands and queries. Controller and class drivers
handle normal transfers independently of that worker.

These documents describe the current implementation, including its lifecycle and
error-handling limits.

| Document | Contents |
| --- | --- |
| [Architecture](architecture.md) | Service boundaries, protocols, worker ordering, ownership and performance. |
| [Common flows](flows.md) | Controller and hub registration, connection, enumeration, queries and removal. |
| [Recovery and lifecycle](recovery-and-lifecycle.md) | Failed enumeration, port isolation, startup, drain/join and regression coverage. |

## Source map

| File | Responsibility |
| --- | --- |
| [main.c](../main.c) | Initialize the core before registering the RPC protocol. |
| [topology.c](../topology.c) | RPC entry points, FIFO queue, command execution and worker lifecycle. |
| [manager.h](../manager.h) | Topology structures and internal core interfaces. |
| [controllers.c](../controllers.c) | Controller registry, USB address bitmap and controller queries. |
| [hubs.c](../hubs.c) | Hub registry, port records, recursive removal and hub configuration. |
| [port_event.c](../port_event.c) | Query current port status and select connection, disconnection or error handling. |
| [devices.c](../devices.c) | Enumeration, device registration, recovery and core cleanup. |

When changing topology behavior, update the relevant flow and its regression
coverage together. Keep protocol behavior separate from internal worker behavior:
waiting for a local command to complete does not add a success response to a
protocol operation that has no response fields.
