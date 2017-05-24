::Setup env
@echo off
setlocal enabledelayedexpansion

::Declare default vars
set arch=x86
set target=vmdk
set decl=0
set buildcfg=i386

::Check arguments
for %%x in (%*) do (
    ::First handle setters (declarators)
    set consumed=0
    if "%%~x"=="-arch" (
        set decl=1
        set consumed=1
    )
    if "%%~x"=="-target" (
        set decl=2
        set consumed=1
    )

    ::Handle arguments
    if "!consumed!"=="0" (
        if "!decl!"=="1" (
            if "%%~x"=="x86" set arch=x86
            if "%%~x"=="X86" set arch=x86
            if "%%~x"=="i386" set arch=x86
        )
        if "!decl!"=="2" (
            set target=%%~x
        )
    )
)

::Build the initial ramdisk
START "initrd-builder" /B /W "%~dp0rdbuild.exe"

::Copy files to install directory
xcopy /v /y %~dp0initrd.mos %~dp0deploy\hdd\system\initrd.mos

::Install MOS
:Install
del %~dp0*.vmdk
del %~dp0*.img

::Run Tool
START "os-installer" /D %~dp0deploy /B /W "%~dp0deploy\diskutility.exe" -auto -target !target! -scheme mbr