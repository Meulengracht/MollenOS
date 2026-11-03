# Virtio network driver architecture

`virtio-net` implements ABI 2 of the `ctt_netadapter` contract for one Ethernet
port per PCI device. netd supplies shared packet pools and ordered batches. The
driver maps those slots for DMA, submits descriptor chains to Virtio RX/TX
queues, and reports admission, completion and retirement progress back to netd.

These documents describe the current source, including its reset and cleanup
behavior. They complement the [netd architecture documents](../../../../services/netd/docs/README.md).
The connection is at the Ethernet frame interface; Internet socket integration
in netd remains unfinished.

| Document | Contents |
| --- | --- |
| [Architecture](architecture.md) | Device setup, execution contexts, memory layout, hardware queues and limits. |
| [Contract implementation](contract.md) | RPC-to-function mapping, session identities, pool validation, replay and completion accounting. |
| [Flows between netd and Virtio](flows.md) | Discovery, startup, TX, RX, link changes, ACKs and completion recovery. |
| [Recovery and lifecycle](recovery-and-lifecycle.md) | Stop/restart, close, DMA failures, removal, diagnostics and test coverage. |

## Source map

| Source | Responsibility |
| --- | --- |
| [main.c](../main.c) | Module protocols, device registry, module lock, fast interrupt entry and deferred event dispatch. |
| [device.c](../device.c) | Capabilities, PCI setup, coherent MAC/link reads, private metadata allocation and device destruction. |
| [interrupt.c](../interrupt.c) | Interrupt/event registration and teardown. |
| [requests.c](../requests.c) | Generated adapter RPC entry points, open/run lifecycle and recovery enumeration. |
| [session.c](../session.c) | Batch validation/replay, packet admission, completion journal, ACKs, stop and close. |
| [pools.c](../pools.c) | SHM attachment, alias checks, per-slot DMA mappings and pool cleanup. |
| [queue.c](../queue.c) | Queue setup/reset, descriptor construction, DMA handoff, RX filtering and polling. |
| [virtio-net.h](../virtio-net.h) | Device/session structures, constants and internal interfaces. |
| [Shared PCI transport](../../common/pci.c) | Modern PCI capabilities, DMA context, feature negotiation and device status. |
| [Shared split queue](../../common/queue.c) | Descriptor allocation, available/used rings, publication and queue teardown. |
| [netadapter.gr](../../../../protocols/contracts/netadapter.gr) | Wire contract and required retry semantics. |
| [CMakeLists.txt](../CMakeLists.txt), [virtio-net.yaml](../virtio-net.yaml) | Build sources, generated bindings, capabilities and PCI device matching. |

Keep wire guarantees separate from transport and hardware completion. An RPC
send is not packet admission, admission is not completion, and a fault does not
establish that DMA has stopped.
