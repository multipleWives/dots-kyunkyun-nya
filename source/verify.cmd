@echo off
setlocal
call "%DOTS_VS%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b 1
cd /d "%DOTS_BUILD%"
cl /nologo /std:c++17 /O2 /MT /EHsc /D _WINDOWS /D MSWindows /D _CRT_SECURE_NO_WARNINGS /I "%DOTS_SDK%\Headers" /I "%DOTS_SDK%\Headers\SP" /I "%DOTS_SDK%\Util" /I "%DOTS_BUILD%" /I "%DOTS_CUDA%\include" "%DOTS_SOURCE%\Verify.cpp" /link /OUT:Verify.exe /LIBPATH:"%DOTS_CUDA%\lib\x64" cuda.lib
if errorlevel 1 exit /b 1
Verify.exe "%DOTS_OUTPUT%\dots-kyunkyun-nya.aex"
exit /b %errorlevel%
