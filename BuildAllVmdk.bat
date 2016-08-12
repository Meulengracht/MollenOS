::Setup Nasm
@set path=C:\Users\Philip\AppData\Local\nasm;%path%

::Setup Environment
CALL "C:\Program Files (x86)\Microsoft Visual Studio 12.0\VC\vcvarsall.bat" x86

::Build Stage1
nasm.exe -f bin boot\Stage1\MFS1\Stage1.asm -o boot\Stage1\MFS1\Stage1.bin

::Build Stage2
START "NASM" /D %~dp0\boot\Stage2 /B /W nasm.exe -f bin Stage2.asm -o ssbl.stm

::Build CLibK
MSBuild.exe clib\mscv\LibRT.sln /p:Configuration=CLibK /t:Clean,Build

::Build MCore
MSBuild.exe kernel\Msvc\MollenOS.sln /p:Configuration=Build_X86_32 /t:Clean,Build

::Build Modules
MSBuild.exe modules\Modules.sln /p:Configuration=Debug /t:Clean,Build

::Build InitRd
START "InitRD" /D %~dp0\modules /B /W "modules\RdBuilder.exe"

::Copy files to install directory
xcopy /v /y kernel\Build\MCore.mos install\Hdd\System\Sys32.mos
xcopy /v /y modules\InitRd.mos install\Hdd\System\InitRd32.mos
xcopy /v /y boot\Stage1\MFS1\Stage1.bin install\Stage1.bin
xcopy /v /y boot\Stage2\ssbl.stm install\ssbl.stm

::Install MOS
START "MOLLENOS INSTALLER" /D %~dp0\install /B /W "install\MfsTool.exe" -vmdk