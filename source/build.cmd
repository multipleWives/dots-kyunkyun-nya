@echo off
setlocal
call "%DOTS_VS%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b 1
cd /d "%DOTS_BUILD%"
"%DOTS_CUDA%\bin\nvcc.exe" --fatbin -O3 --use_fast_math -std=c++17 -I "%DOTS_SOURCE%" -gencode=arch=compute_75,code=sm_75 -gencode=arch=compute_80,code=sm_80 -gencode=arch=compute_86,code=sm_86 -gencode=arch=compute_89,code=sm_89 -gencode=arch=compute_90,code=sm_90 -gencode=arch=compute_100,code=sm_100 -gencode=arch=compute_120,code=sm_120 -gencode=arch=compute_120,code=compute_120 "%DOTS_SOURCE%\DotsKernel.cu" -o kernel.fatbin
if errorlevel 1 exit /b 1
"%DOTS_PWSH%" -NoProfile -File "%DOTS_SOURCE%\embed.ps1" "%DOTS_BUILD%"
if errorlevel 1 exit /b 1
cl /nologo /EP /I "%DOTS_SDK%\Headers" "%DOTS_SOURCE%\DotsPiPL.r" > Dots.rr
if errorlevel 1 exit /b 1
"%DOTS_SDK%\Resources\PiPLtool.exe" Dots.rr Dots.rrc
if errorlevel 1 exit /b 1
cl /nologo /D MSWindows /EP Dots.rrc > Dots.rc
if errorlevel 1 exit /b 1
rc /nologo /fo Dots.res Dots.rc
if errorlevel 1 exit /b 1
cl /nologo /LD /std:c++17 /O2 /MT /EHsc /W4 /D _CRT_SECURE_NO_WARNINGS /D _WINDOWS /D MSWindows /I "%DOTS_SDK%\Headers" /I "%DOTS_SDK%\Headers\SP" /I "%DOTS_SDK%\Util" /I "%DOTS_CUDA%\include" /I "%DOTS_BUILD%" "%DOTS_SOURCE%\Dots.cpp" Dots.res /link /OUT:"%DOTS_OUTPUT%\dots-kyunkyun-nya.aex" /INCREMENTAL:NO /OPT:REF /OPT:ICF /DYNAMICBASE /NXCOMPAT
exit /b %errorlevel%
