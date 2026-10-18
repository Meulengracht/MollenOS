Shared LLVM CMake modules required by the vendored compiler-rt build.

Copied without modification from llvm-project/cmake/Modules at revision
349a932a88a1ab42191d71cd738f8214fcf34993 (LLVM 23.1.1), matching the
compiler-rt source update. See LICENSE.TXT for the LLVM license.

compiler-rt expects these files outside its own source directory in the LLVM
monorepo. Vali supplies this directory through LLVM_COMMON_CMAKE_UTILS so an
OS build does not require a separate LLVM source checkout. LLVM's installed
CMake package is still loaded from the Vali cross-toolchain installation.

When updating compiler-rt, update these shared modules from the same revision
and include any new common-module dependencies it introduces.
