@echo off
rem Build TempTrayNative with MSVC (Visual Studio 2022 Build Tools, "Desktop development with C++").
rem Needs the AMD Ryzen Master SDK installed locally (headers only at build time, DLLs at run time).
rem Override the location with:  set RYZEN_MASTER_SDK_DIR=D:\path\to\RyzenMasterSDK
setlocal
if "%RYZEN_MASTER_SDK_DIR%"=="" set "RYZEN_MASTER_SDK_DIR=C:\Program Files\AMD\RyzenMasterSDK"
call "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
cl /nologo /EHsc /O2 /MD /utf-8 /W3 /I"%RYZEN_MASTER_SDK_DIR%\include" TempTrayNative.cpp ^
   /link /SUBSYSTEM:WINDOWS /MANIFEST:EMBED /MANIFESTINPUT:app.manifest ^
   /MANIFESTUAC:"level='requireAdministrator' uiAccess='false'" user32.lib gdi32.lib advapi32.lib
endlocal
