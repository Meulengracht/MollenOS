::Setup Environment
CALL "C:\Program Files (x86)\Microsoft Visual Studio 12.0\VC\vcvarsall.bat" x86

::Build CLibK
MSBuild.exe clib\mscv\CLib.sln /p:Configuration=CLibK /t:Clean,Build

::Build MCore
MSBuild.exe kernel\Msvc\MollenOS.sln /p:Configuration=Debug /t:Clean,Build

::Build Modules
MSBuild.exe modules\Modules.sln /p:Configuration=Debug /t:Clean,Build

::Build InitRd
START "InitRD" /D %~dp0\modules /B /W "modules\RdBuilder.exe"

::Copy files to install directory
xcopy /v /y kernel\Build\MCore.mos install\Hdd\System\Sys32.mos
xcopy /v /y modules\InitRd.mos install\Hdd\System\InitRd32.mos
