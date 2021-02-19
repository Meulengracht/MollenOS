if (NOT DEFINED VALI_BUILD)
    cmake_minimum_required(VERSION 3.8.2)
    include(../../cmake/SetupEnvironment.cmake)
    project(ValiSerialModules)
endif ()

# build usb serial modules
add_subdirectory(usb/common)
add_subdirectory(usb/ehci)
add_subdirectory(usb/ohci)
add_subdirectory(usb/uhci)
add_subdirectory(usb/hub)
