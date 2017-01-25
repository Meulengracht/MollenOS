::Setup Paths
@echo off
setlocal enabledelayedexpansion
@set path=C:\Users\Philip\AppData\Local\nasm;%path%

::Declare default vars
set arch=x86
set skip=false
set target=vmdk
set decl=0
set buildcfg=Build_X86_32

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
CALL "C:\Program Files (x86)\Microsoft Visual Studio 12.0\VC\vcvarsall.bat" !arch!

::Skip build stage?
if "!skip!"=="true" goto :Install

::Build Stage1
nasm.exe -f bin boot\Stage1\MFS1\Stage1.asm -o boot\Stage1\MFS1\Stage1.bin

::Build Stage2
START "NASM" /D %~dp0\boot\Stage2 /B /W nasm.exe -f bin Stage2.asm -o ssbl.stm

::Build Operation System
MSBuild.exe MollenOS.sln /p:Configuration=!buildcfg! /t:Clean,Build

::Copy files for rd to modules folder
xcopy /v /y librt\build\*.dll modules\build\

::Build InitRd
START "InitRD" /D %~dp0\modules /B /W "modules\RdBuilder.exe"

::Copy files to install directory
xcopy /v /y librt\build\*.dll install\Hdd\System\
xcopy /v /y kernel\Build\MCore.mos install\Hdd\System\Sys32.mos
xcopy /v /y modules\InitRd.mos install\Hdd\System\InitRd32.mos
xcopy /v /y boot\Stage1\MFS1\Stage1.bin install\Stage1.bin
xcopy /v /y boot\Stage2\ssbl.stm install\ssbl.stm

::Install MOS
:Install
START "MOLLENOS INSTALLER" /D %~dp0\install /B /W "install\MfsTool.exe" -!target!