# Raspberry Pi 4/5 native AArch64 boot contract

The primary CPU receives **x0 = physical address of the final flattened device
tree (FDT/DTB)**. There is no VBoot structure, C runtime, filesystem API, or UEFI
system table. Start in assembly, retain x0, establish your own execution
environment, and discover resources from that DTB.

## Image selection and files

Pi 4 normally selects `kernel8.img` for 64-bit boot. Pi 5 prefers
`kernel_2712.img`, falling back to `kernel8.img` if it is absent; use an explicit
`kernel=` to avoid selecting a different file accidentally. Pi 5 only supports
64-bit kernels. Its firmware lives in EEPROM, whereas Pi 4 uses the
`start4.elf`/`fixup4.dat` firmware pair. Keep board DTBs and required overlays
alongside the image. Pi 5 also requires `config.txt` to recognise the boot
partition. [Firmware boot options](https://www.raspberrypi.com/documentation/computers/config_txt.html#boot-options)

For Vali, we produce an **uncompressed flat AArch64 executable image**, with entry
instructions at its beginning. Simply renaming the existing PE `kernel.mos` or
`BOOTAA64.EFI` is not enough: direct Pi boot does not provide Vali's PE loader.
An ELF/PE development artifact may retain symbols, but packaging must produce
the intended flat memory layout. Alternatively, `kernel8.img` could be a small
Vali loader that understands an embedded PE payload.

Do not make `0x80000` an implicit ABI. Older bare-metal examples use it, while
the current official documentation lists `0x200000` as the default 64-bit load
address. Pin placement for the tested firmware/image combination. The filename
alone does not determine placement. [Load-address option](https://www.raspberrypi.com/documentation/computers/legacy_config_txt.html#kernel_address)

`kernel_address` is documented among legacy bare-metal options: record firmware
versions and verify actual entry PC on each board. Do not add `kernel_old=1`,
disable device trees, or override `armstub`. A Linux-compatible Image header is
another valid packaging strategy, but its placement fields and actual load
address must agree with the linker; do not mix that policy with assumptions
from headerless images. A 16 KiB page size is not required merely because the
board is Pi 5 or the filename is `kernel_2712.img`; Vali chooses its own supported
translation granule when it builds page tables.

Use `bcm2711-rpi-4-b.dtb` for Pi 4B and `bcm2712-rpi-5-b.dtb` for Pi 5B from the
matching firmware distribution. Let firmware select/fix up the correct tree.
The DTB file on disk is an input; **the blob addressed by x0 is authoritative**.
The Pi sources explicitly contain memory properties that firmware must fill.
[Pi 4 firmware nodes](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/arch/arm/boot/dts/broadcom/bcm2711-rpi.dtsi),
[Pi 5 board tree](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/arch/arm64/boot/dts/broadcom/bcm2712-rpi-5-b.dts)

## Primary CPU entry

These are the AArch64 Linux-style handoff expectations used by native boot:

| State | What the entry stub may use |
| --- | --- |
| x0 | Physical DTB address; preserve before calls or scratch-register use. |
| x1–x3 | Zero, reserved; not additional memory or board descriptors. |
| x4–x30 | Unspecified; no valid return address or argument contract. |
| SP | No usable kernel stack is promised. Install one before C or stack accesses. |
| PC | Physical image entry; execution is not at Vali's linked higher-half address. |
| Exception level | Non-secure EL2 or EL1; read `CurrentEL`. |
| DAIF | Interrupt classes masked. Keep them masked until handlers exist. |
| MMU | Off. No inherited kernel virtual-address mapping. |
| Caches | Do not assume I-cache is off; loaded code must be coherent. Inspect current-EL SCTLR and normalize deliberately. |
| Counter | Read `CNTFRQ_EL0`; do not substitute CPU core frequency. |

The generic protocol specifies EL1/EL2 and permits an enabled I-cache. It is not
an exhaustive register dump of every Pi firmware release.
[AArch64 boot protocol](https://www.kernel.org/doc/html/latest/arch/arm64/booting.html)

The published Pi 4-capable Arm stub enters **EL2h**, masks DAIF, clears SCTLR_EL2
M/C/I, sets CNTFRQ to 54 MHz for BCM2711, and sets CNTVOFF_EL2 to zero. Its primary
path supplies the DTB and clears x1–x3; secondaries wait outside the kernel.
These are source-backed Pi 4 implementation details, not universal ARM defaults.
The old stub is not a specification of Pi 5's current EEPROM/secure-firmware
register setup. For Pi 5, accept the protocol's EL1/EL2 alternatives and record
the observed EL; this document has not verified a physical Pi 5 entry dump.
[Raspberry Pi Arm stub source](https://github.com/raspberrypi/tools/blob/master/armstubs/armstub8.S)

Vali must initialize its own VBAR, stack selection, EL1 control registers,
translation tables, TLS/CPU-local registers, timer configuration, and FP/SIMD
access policy. Do not treat unused system registers as zero. If entered at EL2,
set up the EL1 transition, trap/routing controls and counter access explicitly;
if entered at EL1, do not execute EL2 register accesses. Never return to the
firmware using `ret`.

Firmware has initialized RAM sufficiently to load and run the image, performed
platform startup, and prepared the boot data. That does not provide zeroed BSS,
constructors, a heap, interrupt handlers, a scheduler, or initialized drivers
for Vali. A configured UART may be usable for diagnostics, but each device needs
an explicit ownership and initialization policy.

## Where to find data

Validate the FDT header at x0: big-endian magic `0xd00dfeed`, `totalsize`, version,
block offsets, and bounds. Decode fields using byte-safe big-endian reads.
Do not cast it to a native little-endian C structure. Save the complete blob or
reserve its backing memory before any allocator can overwrite it.
[FDT binary format](https://devicetree-specification.readthedocs.io/en/stable/flattened-format.html)

| Information | Lookup |
| --- | --- |
| Board/SoC identity | Root `compatible` string list and `model`. |
| RAM banks | All `device_type = "memory"` nodes and their `reg` tuples. |
| Unavailable memory | FDT reservation block plus `/reserved-memory` children. |
| Command line | `/chosen/bootargs`; not an address in x1. |
| Console candidate | `/chosen/stdout-path`, resolving aliases in `/aliases`; tolerate absence. |
| CPUs | `/cpus` children: `reg` affinity, `status`, `enable-method`. |
| Device registers | Node `reg`, translated through parent `ranges`. |
| Interrupts | Interrupt parent and controller-specific interrupt specifiers. |
| Clock/reset dependencies | Phandles and the corresponding provider bindings. |

Respect parent `#address-cells`/`#size-cells`, rather than assuming one 32-bit
cell per field. Preserve full physical addresses. Device-tree paths vary;
identify controllers by compatible strings and bindings.
[Device nodes](https://devicetree-specification.readthedocs.io/en/stable/devicenodes.html),
[Standard properties](https://devicetree-specification.readthedocs.io/en/stable/devicetree-basics.html#standard-properties)

If configured, initrd bounds are advertised by `/chosen/linux,initrd-start` and
`linux,initrd-end` (exclusive end), as defined by the
[chosen-node binding](https://android.googlesource.com/kernel/common/+/fb73974172ff/Documentation/devicetree/bindings/chosen.txt).
Firmware's `initramfs payload.img followkernel`
loads a blob; Vali must interpret its contents. Missing properties mean no
advertised initrd. With a flat image, reserve/pad the entire in-memory footprint:
an omitted BSS or stack area must not overlap a payload placed after the file.
[Initramfs configuration](https://www.raspberrypi.com/documentation/computers/config_txt.html#initramfs)

There is no post-entry firmware file-reading API. If Phoenix or another payload
is needed before storage drivers exist, embed it or place it in the advertised
initrd with a format the Pi entry adapter understands. There is also no ACPI
RSDP or EFI memory map promised on this path. Optional display output must use
an advertised framebuffer or an implemented Pi display/mailbox protocol; a
firmware splash is not a framebuffer descriptor.

## Memory ownership

For Vali's initial allocator, start with advertised RAM banks and subtract:

- FDT reservations and fixed reserved-memory ranges, including `no-map` regions.
- The full kernel image, BSS, stacks, early tables, and bootstrap allocations.
- The DTB and any initrd/payload ranges still in use.
- Firmware/secondary-startup regions, including release mailboxes and code.

Do not assume all memory below the kernel is free. Pi 5's tree reserves the
first `0x80000` bytes for ATF; that region must remain intact for firmware
services. Validate the live reservation rather than using that size as a
universal firmware boundary.
[Pi 5 reserved-memory definition](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/arch/arm64/boot/dts/broadcom/bcm2712.dtsi)

Recommended policy: retain all reserved ranges until their ownership is
understood. Dynamic reserved-memory requests using `size` need explicit
allocation semantics before general allocation starts; they are not fixed
`reg` ranges. Do not reclaim a `reusable` pool without implementing its owner.
Treat bus/DMA addresses separately from CPU physical addresses. Never apply
an old Pi GPU-address masking trick to arbitrary Pi 5 addresses.

## Pi 4 versus Pi 5

| Component | Pi 4B | Pi 5B |
| --- | --- | --- |
| CPU | BCM2711, Cortex-A72 | BCM2712, Cortex-A76 |
| Main interrupt controller | GIC-400 / GICv2 | GIC-400 / GICv2 |
| CPU startup in inspected trees | `spin-table` | `psci`, SMC conduit |
| CPU affinity examples | 0, 1, 2, 3 | 0, 0x100, 0x200, 0x300 |

These entries come from the Raspberry Pi kernel's `rpi-6.12.y` board sources.
Dispatch from the **live DTB**, since firmware, overlays, or another boot chain
can change the description. In particular, `MPIDR_EL1 & 3` is not a portable
CPU-index calculation. Map the full affinity identifier to a logical CPU index.
[BCM2711 definitions](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/arch/arm/boot/dts/broadcom/bcm2711.dtsi),
[BCM2712 definitions](https://github.com/raspberrypi/linux/blob/rpi-6.12.y/arch/arm64/boot/dts/broadcom/bcm2712.dtsi)

For Pi 4 spin tables, obtain each secondary's `cpu-release-addr` from the DTB.
The inspected tree uses `0xe0`, `0xe8`, and `0xf0` for CPUs 1–3. Publish the
physical secondary entry as a 64-bit little-endian value, with cache visibility
and barriers appropriate to the polling CPU, then `sev`. Keep the polling
code/mailbox region reserved. A secondary needs its own stack and must not clear
global BSS again.

For Pi 5 PSCI, use the advertised conduit and full target affinity with CPU_ON;
do not write Pi 4 release addresses. Preserve resident firmware and handle PSCI
errors. The secondary entry argument belongs to the selected startup protocol,
not the primary DTB-pointer contract. Provide a per-CPU bootstrap context.
These are separate implementations behind Vali's CPU-start interface.

Both boards require a **GICv2 backend**, rather than the GICv3 system-register
interface used by the current QEMU `virt` profile. Firmware setup does not
replace kernel IRQ configuration. Read controller addresses and timer interrupt
specifiers from the DTB.

For serial output, Pi 5's default primary UART is on its dedicated debug connector.
Its 40-pin-header UART is an RP1 peripheral, so PCIe/RP1 state matters; it is not
the Pi 4 MMIO device at a new constant address. Prefer the dedicated debug UART
for initial bring-up. Firmware settings and cable detection can change the
routing. Pi 4's default primary UART is the mini UART; selecting a PL011 driver
requires identifying/configuring the appropriate UART, not just `enable_uart=1`.
[UART configuration](https://www.raspberrypi.com/documentation/computers/configuration.html#configure-uarts)

Current firmware provides `enable_rp1_uart=1` to initialize the RP1 UART at 115200
and retain RP1 state; normally the RP1 PCIe controller is reset before OS entry.
Use that only as an explicitly recorded debug configuration, not as a substitute
for Vali's RP1 initialization.
[RP1 boot options](https://www.raspberrypi.com/documentation/computers/config_txt.html#enable_rp1_uart)

## QEMU

QEMU **11.1.1** lists `raspi4b` (revision 1.5); it does not list
a Pi 5 machine. Upstream documents a Pi 4B model with four Cortex-A72 cores and
2 GiB RAM. PCIe root-port and GENET Ethernet emulation are missing, so successful
CPU/serial testing cannot establish full board support.
[QEMU Raspberry Pi support](https://www.qemu.org/docs/master/system/arm/raspi.html)

Example direct-boot command for a raw image linked at `0x80000`, with a matching
Pi 4B DTB supplied explicitly (paths are examples, not existing build outputs):

```sh
qemu-system-aarch64 \
  -M raspi4b -m 2G -smp 4 \
  -kernel /path/to/kernel8.img \
  -dtb /path/to/bcm2711-rpi-4-b.dtb \
  -display none -serial stdio -monitor none \
  -no-reboot
```

Add `-S -gdb tcp:127.0.0.1:1234` for an initial halt and debugger connection.
Use the symbol-bearing artifact for breakpoints, not the stripped flat file.
The first QEMU serial backend is the PL011; guest UART selection must match it.
This command is a proposed test invocation; only model availability was checked
here, not a boot of Vali's image.

`-kernel` uses QEMU's direct Arm loader and synthetic startup support. It does
**not** run the complete Pi EEPROM/VideoCore boot chain or process `config.txt`
and overlays as real firmware would. QEMU can modify the supplied DTB, including
memory and unimplemented devices. Always parse x0, even in the emulator.
[QEMU Pi startup](https://github.com/qemu/qemu/blob/master/hw/arm/raspi.c),
[Pi 4 DTB adjustments](https://github.com/qemu/qemu/blob/master/hw/arm/raspi4b.c)

Use QEMU for the native entry adapter, DT parsing, MMU, GICv2, and Pi 4 secondary
startup. Use physical Pi 4/5 for their actual firmware entry state, clocks,
cache/DMA behavior, and board devices. `virt` remains useful for generic ARM64
tests, but it is neither a Pi 4 nor a Pi 5 hardware test.
