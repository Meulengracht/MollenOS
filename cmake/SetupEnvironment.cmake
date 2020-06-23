# Make sure all the proper env are set
set(CMAKE_SYSTEM_NAME vali-cross)
set(VERBOSE 1)

# Setup shared compile flags to make compilation succeed
# -Xclang -flto-visibility-public-std
set(VALI_DEFINITIONS -U_WIN32 -DMOLLENOS)
set(VALI_COMPILE_FLAGS -fms-extensions -nostdlib -nostdinc)
if("$ENV{VALI_ARCH}" STREQUAL "i386")
    set(VALI_DEFINITIONS ${VALI_DEFINITIONS} -Di386 -D__i386__)
    set(VALI_COMPILE_FLAGS ${VALI_COMPILE_FLAGS} -m32 --target=i386-pc-win32-itanium-coff)
elseif("$ENV{VALI_ARCH}" STREQUAL "amd64")
    set(VALI_DEFINITIONS ${VALI_DEFINITIONS} -Damd64 -D__amd64__ -Dx86_64 -D__x86_64__)
    set(VALI_COMPILE_FLAGS ${VALI_COMPILE_FLAGS} -m64 --target=amd64-pc-win32-itanium-coff -fdwarf-exceptions)
endif()
string(REPLACE ";" " " VALI_DEFINITIONS "${VALI_DEFINITIONS}")
string(REPLACE ";" " " VALI_COMPILE_FLAGS "${VALI_COMPILE_FLAGS}")

# We need to preserve any flags that were passed in by the user. However, we
# can't append to CMAKE_C_FLAGS and friends directly, because toolchain files
# will be re-invoked on each reconfigure and therefore need to be idempotent.
# The assignments to the _INITIAL cache variables don't use FORCE, so they'll
# only be populated on the initial configure, and their values won't change
# afterward.
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${VALI_COMPILE_FLAGS} ${VALI_DEFINITIONS}" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${VALI_COMPILE_FLAGS} ${VALI_DEFINITIONS}" CACHE STRING "" FORCE)
