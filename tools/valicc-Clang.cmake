##################################################
# Setup compiler environment
##################################################
# This module is shared by multiple languages; use include blocker.
if(__VALI_CC_CLANG)
    return()
endif()
set(__VALI_CC_CLANG 1)

set(VALI_PLATFORM_FLAGS "-fms-extensions --target=$ENV{VALI_ARCH}-uml-vali")

##################################################
# SHARED (LANGUAGES)
##################################################
macro(__valicc_compiler lang)
    # no -fPIC on Vali
    set(CMAKE_${lang}_COMPILE_OPTIONS_PIC "")
    set(CMAKE_${lang}_COMPILE_OPTIONS_PIE "")
    set(_CMAKE_${lang}_PIE_MAY_BE_SUPPORTED_BY_LINKER NO)
    set(CMAKE_${lang}_LINK_OPTIONS_PIE "")
    set(CMAKE_${lang}_LINK_OPTIONS_NO_PIE "")
    set(CMAKE_SHARED_LIBRARY_${lang}_FLAGS "-D_DLL")
    set(CMAKE_${lang}_STANDARD_LIBRARIES "")

    #set(CMAKE_${lang}_LINKER_WRAPPER_FLAG "-Xlinker" " ")
    #set(CMAKE_${lang}_LINKER_WRAPPER_FLAG_SEP)
    set(CMAKE_${lang}_COMPILE_OBJECT "<CMAKE_${lang}_COMPILER> <DEFINES> <INCLUDES> ${VALI_PLATFORM_FLAGS} <FLAGS> -o <OBJECT> -c <SOURCE>")
    if(NOT "${lang}" STREQUAL "ASM")
        set(CMAKE_${lang}_CREATE_SHARED_LIBRARY "<CMAKE_${lang}_COMPILER> <CMAKE_SHARED_LIBRARY_${lang}_FLAGS> <LANGUAGE_COMPILE_FLAGS> <LINK_FLAGS> --target=$ENV{VALI_ARCH}-uml-vali -Xlinker -implib:<TARGET_IMPLIB> -Xlinker -lldmap -Xlinker -version:<TARGET_VERSION_MAJOR>.<TARGET_VERSION_MINOR> <CMAKE_SHARED_LIBRARY_CREATE_C_FLAGS> -o <TARGET> <OBJECTS> <LINK_LIBRARIES>")
        set(CMAKE_${lang}_CREATE_SHARED_MODULE ${CMAKE_${lang}_CREATE_SHARED_LIBRARY})
        set(CMAKE_${lang}_LINK_EXECUTABLE "<CMAKE_${lang}_COMPILER> <FLAGS> <CMAKE_${lang}_LINK_FLAGS> <LINK_FLAGS> --target=$ENV{VALI_ARCH}-uml-vali -Xlinker -lldmap -Xlinker -version:<TARGET_VERSION_MAJOR>.<TARGET_VERSION_MINOR> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>")
    endif()
endmacro()
