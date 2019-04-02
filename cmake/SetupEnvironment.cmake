# Make sure all the proper env are set
set(CMAKE_SYSTEM_NAME Vali)
set(CMAKE_CROSSCOMPILING OFF CACHE BOOL "")
set(CMAKE_C_COMPILER "$ENV{CROSS}/bin/clang" CACHE FILEPATH "")
set(CMAKE_CXX_COMPILER "$ENV{CROSS}/bin/clang++" CACHE FILEPATH "")
set(CMAKE_LINKER "$ENV{CROSS}/bin/lld-link" CACHE FILEPATH "")
set(CMAKE_AR "$ENV{CROSS}/bin/llvm-ar" CACHE FILEPATH "")
set(CMAKE_RANLIB "$ENV{CROSS}/bin/llvm-ranlib" CACHE FILEPATH "")
set(VERBOSE 1)

# Setup shared compile flags to make compilation succeed
# -Xclang -flto-visibility-public-std
set(VALI_LLVM_COMPILE_FLAGS -U_WIN32 -fms-extensions -nostdlib -nostdinc -DMOLLENOS -DZLIB_DLL)
if("$ENV{VALI_ARCH}" STREQUAL "i386")
    set(VALI_LLVM_COMPILE_FLAGS ${VALI_LLVM_COMPILE_FLAGS} -Di386 -D__i386__ -m32 --target=i386-pc-win32-itanium-coff)
elseif("$ENV{VALI_ARCH}" STREQUAL "amd64")
    set(VALI_LLVM_COMPILE_FLAGS ${VALI_LLVM_COMPILE_FLAGS} -Damd64 -D__amd64__ -D__x86_64__ -m64 -fdwarf-exceptions --target=amd64-pc-win32-itanium-coff)
endif()
string(REPLACE ";" " " VALI_LLVM_COMPILE_FLAGS "${VALI_LLVM_COMPILE_FLAGS}")

# We need to preserve any flags that were passed in by the user. However, we
# can't append to CMAKE_C_FLAGS and friends directly, because toolchain files
# will be re-invoked on each reconfigure and therefore need to be idempotent.
# The assignments to the _INITIAL cache variables don't use FORCE, so they'll
# only be populated on the initial configure, and their values won't change
# afterward.
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${VALI_LLVM_COMPILE_FLAGS}" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${VALI_LLVM_COMPILE_FLAGS}" CACHE STRING "" FORCE)
