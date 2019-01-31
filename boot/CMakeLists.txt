# Build the stage 1 bootloader for BIOS
if (X86_MBOOT_FAT32)
    set (SRCS stage1/fat32/stage1.asm)
else
    set (SRCS stage1/mfs/stage1.asm)
endif
add_library (stage1 ${SRCS})
set_target_properties (stage1 PROPERTIES SUFFIX ".sys")

# Build the stage 2 bootloader for BIOS
set (SRCS stage2/stage2.asm)
add_library (stage2 ${SRCS})
set_target_properties (stage2 PROPERTIES SUFFIX ".sys")

# Build the trampoline code for the cpu
set (SRCS stage2/ap.asm)
add_library (ap ${SRCS})
set_target_properties (ap PROPERTIES SUFFIX ".sys")
