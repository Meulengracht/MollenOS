::Setup Paths
@echo off
setlocal enabledelayedexpansion
@set path=C:\Users\Philip\AppData\Local\nasm;%path%

::Declare default vars
set arch=x86
set skip=false
set target=vmdk
set decl=0
set action=build
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
    if "%%~x"=="-auto" (
        set decl=0
        set target=a
	set consumed=1
    )
    if "%%~x"=="-install" (
        set decl=0
        set skip=true
        set consumed=1
    )
    if "%%~x"=="-rebuild" (
        set decl=0
        set action=rebuild
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

::Setup Environment
CALL "C:\Program Files (x86)\Microsoft Visual Studio\2017\Community\VC\Auxiliary\Build\vcvarsall.bat" !arch!

::Skip build stage?
if "!skip!"=="true" goto :Install

::Build Stage1
nasm.exe -f bin %~dp0\boot\stage1\mfs\stage1.asm -o %~dp0\boot\stage1\mfs\stage1.sys

::Build Stage2
START "NASM" /D %~dp0\boot\stage2 /B /W nasm.exe -f bin stage2.asm -o stage2.sys

::Build Operation System
if "!action!"=="rebuild" MSBuild.exe %~dp0\MollenOS.sln /p:Configuration=!buildcfg! /p:Platform="MollenOS" /t:Clean,Build
if "!action!"=="build" MSBuild.exe %~dp0\MollenOS.sln /p:Configuration=!buildcfg! /p:Platform="MollenOS" /t:Build

::Copy files for rd to modules folder
xcopy /v /y %~dp0\librt\build\*.dll %~dp0\modules\build\

::Build InitRd
START "InitRD" /D %~dp0\modules /B /W "%~dp0\modules\RdBuilder.exe"

::Copy files to install directory
xcopy /v /y %~dp0\librt\build\*.dll %~dp0\install\hdd\system\
xcopy /v /y %~dp0\kernel\build\MCore.mos %~dp0\install\hdd\system\syskrnl.mos
xcopy /v /y %~dp0\modules\InitRd.mos %~dp0\install\hdd\system\initrd.mos
xcopy /v /y %~dp0\boot\stage1\mfs\stage1.sys %~dp0\install\stage1.sys
xcopy /v /y %~dp0\boot\stage2\stage2.sys %~dp0\install\stage2.sys

::Install MOS
:Install
del %~dp0\*.vmdk
del %~dp0\*.img

::Run Tool
START "MOLLENOS INSTALLER" /D %~dp0\install /B /W "%~dp0\install\diskutility.exe" -auto -target !target! -scheme mbr