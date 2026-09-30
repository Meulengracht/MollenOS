# This CMakeLists.txt file is used to install the Raspberry Pi bootloader.
# We only copy all the included boot-asset files to the deployment directory.
# The boot asset files contain
# - start.elf
# - fixup.dat
# - bootcode.bin
# - kernel.img
# - config.txt
# - cmdline.txt
# - overlays/
# - *.dtb
# They will be installed into ${VALI_PATH_BOOT_ASSETS}

install(
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
    DESTINATION ${VALI_PATH_BOOT_ASSETS}
    FILES_MATCHING
    PATTERN "start*.elf"
    PATTERN "fixup*.dat"
    PATTERN "bootcode.bin"
    PATTERN "config.txt"
    PATTERN "cmdline.txt"
    PATTERN "overlays"
    PATTERN "*.dtb"
)
