# Network service architecture

netd implements local sockets and manages Ethernet adapter sessions. Socket
control uses the `sys_socket` service at `/service/net`. Adapter control and
packet descriptors use the `ctt_netadapter` contract; frame bytes travel through
registered shared-memory pools.

These documents describe the current implementation. Local sockets carry data
between processes. Internet socket creation still returns `OS_ENOTSUPPORTED`,
and normal service startup does not install an IP stack in the adapter callbacks.
An adapter reaching `RUNNING` therefore does not imply working TCP/IP sockets.

| Document | Contents |
| --- | --- |
| [Architecture](architecture.md) | Service boundaries, socket domains, execution contexts and integration points. |
| [Adapter infrastructure](adapter-infrastructure.md) | Sessions, requests, buffer layout, packet lifetimes, batching and limits. |
| [Common flows](flows.md) | Discovery, startup, TX, RX, local socket connection and data transfer. |
| [Recovery and lifecycle](recovery-and-lifecycle.md) | Retries, completion recovery, stop/restart, close, removal and diagnostics. |

## Source map

| Source | Responsibility |
| --- | --- |
| [main.c](../main.c), [netd.yaml](../netd.yaml) | Service initialization, device subscription and discovery. |
| [manager.c](../manager.c) | Socket RPC handlers, socket registry and notification worker. |
| [socket.c](../socket.c), [socket.h](../socket.h) | Socket objects, shared send/receive pipes and cleanup. |
| [domains/domains.c](../domains/domains.c), [domains/local.c](../domains/local.c) | Domain dispatch and local socket communication. |
| [adapters/adapters.h](../adapters/adapters.h), [adapters/adapters.c](../adapters/adapters.c) | Synchronized service API, port registry and adapter worker. |
| [adapters/adapter.h](../adapters/adapter.h), [adapters/adapter.c](../adapters/adapter.c) | Transport-independent session API, lifecycle and control replies. |
| [adapters/private.h](../adapters/private.h), [adapters/session.h](../adapters/session.h), [adapters/lease.h](../adapters/lease.h) | Internal state and helper interfaces. |
| [adapters/scheduler.c](../adapters/scheduler.c) | Select the next control, batch, ACK or drain request. |
| [adapters/requests.c](../adapters/requests.c), [adapters/client.c](../adapters/client.c) | Generated Gracht calls, reply polling and directed event handlers. |
| [adapters/events.c](../adapters/events.c) | Validate admissions, completions, drain results and progress. |
| [adapters/queue.c](../adapters/queue.c) | Allocate packet resources, build batches and dispatch final packet results. |
| [adapters/tx.c](../adapters/tx.c), [adapters/rx.c](../adapters/rx.c) | Direct/copy TX and consumer RX delivery. |
| [adapters/buffers.c](../adapters/buffers.c), [adapters/buffers.h](../adapters/buffers.h) | Shared pools, slot leases and completion accounting. |
| [netadapter.gr](../../../protocols/contracts/netadapter.gr) | Driver contract, wire structures and replay rules. |
| [Virtio network driver](../../../modules/virtio/net/docs/README.md) | Driver architecture, contract implementation and flows back to netd. |
| [CMakeLists.txt](../CMakeLists.txt), [test targets](../../../testing/CMakeLists.txt) | Production source list and optional regression suites. |

When changing a protocol or packet lifetime, update the corresponding flow and
recovery rules here. The contract defines wire behavior; these documents explain
how netd implements and uses it.
