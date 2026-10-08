# Device service boundaries

The service remains one CMake target. Its source lists follow `core`, `firmware`,
`bus/legacy`, `bus/pci`, `bus/pci/hosts`, and `bus/rp1`. Only `deviced` receives its
service-root and `include` search paths. Service headers use paths such as
`<bus/pci/host.h>`; no PCI-directory search path is needed. The shared device-tree
reader remains the existing `vali-fdt` component.

## PCI interfaces

| Header | Responsibility |
| --- | --- |
| `discovery.h` | Find controllers and start scanning at service startup. |
| `host.h` | Host identification, registration, lifetime, and I/O resource policy. |
| `hosts/ecam.h`, `hosts/legacy.h` | Construct controllers without scanning or registration. |
| `host-private.h` | PCI-owned controller operations, host state, lists, firmware references, and locks. |
| `device.h` | Discovered PCI functions and their parent/child relationships. |
| `registers.h` | Hardware register layout and PCI class/command constants. |
| `config.h` | Read and write configuration registers through a host. |
| `bars.h` | Describe and register PCI address ranges. |
| `function.h` | Attach a function-specific bus such as RP1. |
| `enumerate.h` | Scan buses and follow bridges. |
| `publish.h` | Add or remove a complete host's device descriptions. |
| `interrupts.h` | Resolve a function's interrupt route. |
| `strings.h` | Convert PCI classes to names and device-manager class values. |

Include the roles a file uses directly. RP1 reads host identity through
`PciHostGetIdentification`; it does not include the controller's private state.
Fixed legacy device IDs belong to `bus/legacy/fixed.h`, and configuration-port
addresses remain inside `bus/pci/hosts/legacy.c`.

## Ownership and driver matching

Host registration assigns `PciHostIdentification.HostId` only after checking for
conflicting bus ranges and allocating the root. A failed registration leaves the
controller with its caller. Firmware mappings and controller resources remain
owned until registered devices and their outstanding requests have been removed.

`core/publication.h` manages one group for each PCI host, including RP1 children.
Descriptions are added with driver matching disabled. Finishing the group marks
the complete set ready; enabling binding then allows drivers to match. Successful
binding steps are remembered across retries. Removal proceeds from children to
parents and keeps unfinished entries alive if a request or child prevents removal.
Adding entries and starting drivers are blocked once removal has begun.

For numeric device identities, a vendor/product match takes precedence over a
class/subclass match, regardless of driver list order. Platform devices continue
to prefer the first supported compatible string in firmware order.

## Validation status

Host identification/registration, shared publication, request lifetime handling,
and these header/build boundaries are implemented. The `testing/firmware`
harnesses exercise them using modeled controllers and, for registry integration,
the real device registry and driver discovery code. See that directory's README
for the cases covered. These tests and a service build do not establish physical
hardware support. Hardware validation, additional buses, and future lifecycle
extensions require their own implementation and testing.
