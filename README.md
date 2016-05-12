
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

## Building MollenOS

### Pre-requisites
In order to build MollenOS you need NASM installed on your system in order to assemble the bootloader and various assembler files in the visual studio projects, you also need Visual Studio 2013 installed on your system to build the projects. These are the only programs needed in order to build MollenOS, drivers, userspace etc. 

### Modifying the build script
The build process is normally <almost> fully automated, but before you run BuildAll.bat you need to modify it to suit your paths, etc.

1. The first thing you should do is edit the script and fix the paths for your own computer.
2. The second thing you should do is under the "::Install MOS" step, remove the "-a" option, because THIS WILL install MollenOS directly the first disk it discovers, which will be your primary disk. I have it hardcoded in that it skips my primary disk for convience, and thus I'm the only one that can use that option.
3. No more things you can do, the build script now builds and updates everything in the /install/ folder.
4. You MUST grant MfsTool.exe administrator privelieges in order for it access disk drives, the program is located in /install/

### Building MollenOS
Just run the BuildAll.bat after you've modified it. It will as an end-step automatically start the install program for MollenOS after the build process.

### Installing MollenOS
The installer is started automatically by BuildAll.bat at the end, and the installer will present you with the available disks in your system, in a numbered fashion. To format a disk with MFS type the following in the command-line:
1. format <diskno> (Example: format 1)
2. install <diskno> (Example: install 1)

These two commands is all that is needed, when the program stops writing files to the disk, your disk is now setup for MollenOS
