CALL "C:\Program Files (x86)\Microsoft Visual Studio 12.0\VC\vcvarsall.bat" x86

MSBuild.exe mscv\CLib.sln /p:CLToolPath="C:\Users\Philip\Desktop\MollenOS\Bin\Compiler\msbuild-bin";Configuration=CLib /t:Clean,Build
pause