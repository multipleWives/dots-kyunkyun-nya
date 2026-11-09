param(
    [Parameter(Mandatory=$true)][string]$SdkExamples,
    [string]$CudaPath='C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.2',
    [string]$BuildDir="$PSScriptRoot\..\build",
    [string]$OutputDir="$PSScriptRoot\..\dist"
)
$ErrorActionPreference='Stop'
$vswhere='C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
$vs=& $vswhere -version '[17.0,18.0)' -products '*' -property installationPath | Select-Object -First 1
if(!$vs){throw 'Visual Studio 2022 C++ build tools are required.'}
New-Item -ItemType Directory -Force $BuildDir,$OutputDir | Out-Null
$env:DOTS_BUILD=(Resolve-Path $BuildDir).Path
$env:DOTS_OUTPUT=(Resolve-Path $OutputDir).Path
$env:DOTS_SDK=(Resolve-Path $SdkExamples).Path
$env:TEMP=$env:DOTS_BUILD
$env:TMP=$env:DOTS_BUILD
$env:DOTS_SOURCE=$PSScriptRoot
$env:DOTS_CUDA=$CudaPath
$env:DOTS_VS=$vs
$env:DOTS_PWSH=(Get-Process -Id $PID).Path
& cmd /d /c "$PSScriptRoot\build.cmd"
if($LASTEXITCODE -ne 0){throw "Build failed with exit code $LASTEXITCODE"}
