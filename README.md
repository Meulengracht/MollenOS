
# About MollenOS

MollenOS is a hobby OS project, started back in 2011. The aim is to create something usuable, just for trivial tasks in your every normal day like checking the web.
It is written entirely from scratch, however the C Library is a custom variation of third party existing c libraries and my own implementations.

## Kernel Features

Since MollenOS uses it's own filesystem (MFS), it is not booted by the more traditional way of GRUB. Instead it has it's own advanced bootloader, which can be found under my other repositories. mBoot is written specifically for MollenOS, and supports booting from both FAT32 & MFS.

MollenOS is written in the traditional layers (The kernel is built upon a hardware abstraction layer), which means it's easy to architectures to MollenOS and support any platform. The kernel has become modular (with modules transitioning to userspace at some point), which means it only loads device drivers that are actually present on the computer.

MollenOS supports a wide array of features and has implementation for VFS, Processes, Pipes, an advanced PE loader (which is used as the file format in MollenOS), ACPICA built in and MollenOS natively uses UTF-8 in it's kernel. UTF-8 Is implemented in a library called MString which is written for MollenOS.

## Drivers:
    - ACPICA
    - MFS
    - HPET
    - USB Stack (OHCI, UHCI, EHCI)
    - USB MSD
    - USB HID
    - (x86) PCI/PCIe
    - (x86) CMOS
    - (x86) PIT
    - (x86) RTC
    - (x86) PS2 Mouse & Keyboard
    - (x86) APIC

## Userspace

Userspace is still being fleshed out, and not much work has been done here yet.
