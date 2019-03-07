
# The Mollen(/Vali) Operating System 

[![Build Status](http://www.mollenos.com/teamcity/app/rest/builds/buildType:(id:ValiOS_I386_Build)/statusIcon)](http://www.mollenos.com/teamcity/project.html?projectId=ValiOS&branch_ValiOS=__all_branches__)
[![Build Status](http://www.mollenos.com/teamcity/app/rest/builds/buildType:(id:ValiOS_Amd64_Build)/statusIcon)](http://www.mollenos.com/teamcity/project.html?projectId=ValiOS&branch_ValiOS=__all_branches__)

## Getting Started

### Environment variables
Before you setup anything you must setup environmental variables that are used by
the project.

| Variable         | Description             |
| ---------------- |:-----------------------:|
| CROSS            | Points to where the cross-compiler will be installed. |
| VALI_ARCH        | Which architecture you will build the OS and applications for. |

### Setting up the toolchain
The only thing you need to get started is a succesfully built toolchain of llvm/clang/lld. To help make this easier
I have made a fully automated script, which downloads all the neccessary components, and initiates a full build of llvm/lld/clang.
Make note that two full builds need to run, and that this takes a couple of hours. First the llvm/clang/lld setup is built and installed,
then the cross-compiler is built afterwards.

Toolchain scripts are located [here](https://github.com/Meulengracht/vali-toolchain). You should run the scripts in this order:
- depends.sh
- checkout.sh
- build-clang.sh
- build-cross.sh

### Setting up for OS development
The last step is now to run the depends.sh script that is located in this repository which installs
the final pre-requisites (nasm, mono-complete, cmake platform script). The script is located in tools/depends.sh.

After this, you are essentially ready to start developing on the operating system. When/if you make pull requests
when contributing, please follow the pull template that is provided.

### The build commands
There is a series of build commands available.

| Command           | Description             |
| ----------------- |:-----------------------:|
| make              | Builds the operating system and support libraries |
| make install_img  | Creates a harddisk image with bootloader, kernel, libraries and built apps of format .img |
| make install_vmdk | Creates a harddisk image with bootloader, kernel, libraries and built apps of format .vmdk |

## Current development progress

Progress so far is that the kernel has succesfully been converted to a hybrid micro-kernel. Drivers have all been fitted to the new driver framework and
are compiling. The new toolchain has also been taken into use (llvm/clang/lld) and i am currently working on a native port of said toolchain. The focus
for 2019 will be the userspace, and stability/robustness of the operating system. No new kernel features are planned for the OS at this moment,
and no new drivers unless I should reach a point where they are highly required.

## Project Structure

- /boot (Contains bootloaders and anything boot-related)
- /cmake (CMake configuration files required to build the OS with cmake)
- /config (Make configuration files required to build the OS with make)
- /docs (Documentation and related resources about the project and the OS)
- /kernel (Contains the MollenOS kernel source code)
- /librt (Contains all support and runtime libraries needed for MollenOS)
- /modules (Contains drivers for MollenOS)
- /releases (Full releases of the OS)
- /services (Contains system services like the filemanager for MollenOS)
- /tools (Contains tools for building and manipulating)
- /userspace (Contains software projects for the user-applications)
- /resources (Contains the deploy folder for installing the OS)

## Operating System Features

### Boot
MollenOS uses it's own filesystem (MFS), it is not booted by the more traditional way of GRUB. Instead it has it's own advanced bootloader, which can be found in the /boot directory. mBoot is written specifically for MollenOS, and supports booting from both FAT32 & MFS.

### Kernel
MollenOS supports a wide array of features and has implementation for VFS, Processes, Pipes, an advanced PE loader (which is used as the file format in MollenOS), ACPICA built in and MollenOS natively uses UTF-8 in it's kernel. UTF-8 Is implemented in a library called MString which is written for MollenOS.

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

![Custom bootloader loading mollenos](docs/images/bootloader.png)

![The usb stack enumerating an usb-port](docs/images/usbstack.png)

### Usage

![The mesa/llvmpipe/gallium port for the OS running](docs/images/gfx1.png)
![The mesa/llvmpipe/gallium port for the OS running](docs/images/gfx2.png)
![The mesa/llvmpipe/gallium port for the OS running](docs/images/gfx4.png)

## Implementations & Essential Software

### The C-Library, C++ Library and OS Library

The run-time libraries used/implemented in mollenos are:
 - libcrt (Contains compiler support functions and entry points for apps/drivers/etc)
 - libclang (Contains compiler support for clang)
 - libc + libm (Contains the standard c library support for the c-language)
 - libcxx + libcxxabi (Contains the standard c++ library runtime and support for the c++-language)

The c++ runtime and support is a native port of the c++ runtime for clang and the itanium abi. All os support functions are available in the standard c library.

### Vioarr (Window Manager)

Vioarr (formerly known as Sapphire), is the windowmanager for MollenOS. It will be based upon a 3d platform (opengl) to perform advanced and beautiful
graphical user interfaces. Vioarr currently builds on the vector graphics library nanovg which will be used for doing UI elements. The user interface in
MollenOS will be with focus on simplicity with a modern theme. 

### Ported libraries and programs

These are the various libraries ported to MollenOS userspace, and are primarily used by system software, like the terminal, window manager etc.

| Library       | Version   | Description             |
| ------------- | ---------:|:-----------------------:|
| openlibm      | <unk>     | open mathematical c-library   |
| compiler-rt   | <unk>     | llvm/clang compiler runtime support  |
| libunwind     | <unk>     | unwind support for the c++ itanium abi  |
| libcxxabi     | <unk>     | c++ itanium abi compiler support implementation  |
| libcxx        | <unk>     | c++ standard library support   |
| llvm          | <unk>     | The LLVM compiler back-end   |
| mesa3d        | <unk>     | Open source 3d opengl framework   |
| zlib          | <unk>     | Compression library   |
| libpng        | <unk>     | PNG image library   |
| freetype2     | <unk>     | Library to handle and render fonts   |

