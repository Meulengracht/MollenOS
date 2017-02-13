
# About MollenOS

## The Goal

The goal with MollenOS is to provide users with a free, modern operating system, capable of running on as many platforms as possible. Initially the goal will be to support the most popular platforms (Arm and x86). The focus will be in the user-experience as soon as I get that far with MollenOS.

## How it started

MollenOS is a OS project that started back in 2011 as a hobby project. I then took a long break and picked up development again in the start of 2015 by rewriting the entire OS structure as it became apparant to me the initial design was really bad and not scalable nor modular enough to support multiple platforms. The project has then been in development from 2015, to the current date (as of writing this).

## Current Progress

Currently, the focus is on the modularity of MollenOS and its modules (/drivers). A lot of time is going into modelling and designing driver API a long with the user-API on how to use these drivers. The result of this will also be a unified driver framework that will allow any developer to easily write new drivers for MollenOS and contribute to the OS in that way.

Once the drivers and the driver-framework are in place, focus can be moved to userspace, and building the programming model along with essential userspace applications. (Window manager, Terminal driver etc)

## Project Structure

- /boot (Contains bootloaders and anything boot-related)
- /docs (Contains documentation about the project and the OS)
- /install (Contains the deploy folder for installing the OS)
- /kernel (Contains the MollenOS kernel source code)
- /librt (Contains all support and runtime libraries needed for MollenOS)
- /modules (Contains drivers for MollenOS)
- /servers (Contains system services like the filemanager for MollenOS)
- /tools (Contains tools for building and manipulating)
- /userspace (Contains software projects for the user-applications)

## Features

MollenOS uses it's own filesystem (MFS), it is not booted by the more traditional way of GRUB. Instead it has it's own advanced bootloader, which can be found in the /boot directory. mBoot is written specifically for MollenOS, and supports booting from both FAT32 & MFS.

MollenOS supports a wide array of features and has implementation for VFS, Processes, Pipes, an advanced PE loader (which is used as the file format in MollenOS), ACPICA built in and MollenOS natively uses UTF-8 in it's kernel. UTF-8 Is implemented in a library called MString which is written for MollenOS.

### MollenOS FileSystem (MFS)

ToDo

### MString Library

ToDo

## Drivers:
    - AHCI
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

## Documentation

All documentation about design and implementation, and the theory behind is stored in the `/docs` folder. Right now there isn't any documentation, but it'll all come with the Documentation milestone.

## Screenshots

Showcase of MollenOS to get an idea of how the userspace will be once it's finished.

### Boot Screen

ToDo

### Usage

ToDo

## Implementations & Essential Software

### The C-Library, C++ Library and OS Library

ToDo

### Sapphire (Window Manager)

ToDo

### Terminal Implementation

ToDo

### Ported libraries and programs

These are the various libraries ported to MollenOS userspace, and are primarily used by system software, like the terminal, window manager etc.

| Library       | Version   | Description             |
| ------------- | ---------:|:-----------------------:|
| openlibm      | <unk>     | Portable Math Library   |
| zlib          | 1.2.8     | Compression library, used by libpng |
| libpng        | 1.6.26    | Library for handling *.png image files |
| libjpeg       | 9b        | Library for handling *.jpg image files |
| freetype2     | 2.7.0     | Library for handling and rendering truetype fonts |
| SDL2          | 2.0.3     | Graphics/Utility library used by Sapphire for rendering |
| SDL2_image    | 2.0.1     | Image helper library used by Sapphire for rendering |
| lua           | 5.3.2     | I ported this just for fun so I had something to test when the terminal implementation is ready |


## Building MollenOS

### Pre-requisites
In order to build MollenOS you need NASM installed on your system in order to assemble the bootloader and various assembler files in the visual studio projects, you also need Visual Studio 2017 installed on your system to build the projects. These are the only programs needed in order to build MollenOS, drivers, userspace etc. 

### Build Script Information
The build and install process is almost fully automated on windows, and is controlled by Build.bat and MfsTool.exe, the only thing you have to run is the Build.bat. In order to customize your installation and build process, there is a number of switches you can give to Build.bat

#### Build Arguments
`-arch` This switch allows you to specify which platform you want to build for, right now it defaults to the `x86` platform. At this moment, it's also the only supported platform. Valid parameters for it are `i386`, `x86` and `X86`

`-target` This switch allows you to control which medium you want MollenOS installed to. It always default to the creation of a VMDK image file, to use with virtual computers. If you want it installed to a live disk, use `live` as an argument, where MfsTool will automatically be run and allow you to specify which disk you want to use. Other valid targets are `img` (image file).

`-install` The install switch allows you to skip the building of the entire MollenOS project and go directly to the creation of the disk image / disk installation. This is used as a shortcut when no code-changes has been made, but rather only disk image changes. 


#### MfsTool Instructions (Use with `-target live`)
The installer is started automatically by Build.bat at the end, and the installer will present you with the available disks in your system, in a numbered fashion. To format a disk with MFS type the following in the command-line:

1. format disk_no (Example: format 1)
2. install disk_no (Example: install 1)

These two commands is all that is needed, when the program stops writing files to the disk, your disk is now setup for MollenOS
