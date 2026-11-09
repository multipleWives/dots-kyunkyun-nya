param(
    [Parameter(Mandatory=$true)][string]$SdkExamples,
    [string]$CudaPath='C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.2',
    [string]$BuildDir="$PSScriptRoot\..\build",
    [string]$OutputDir="$PSScriptRoot\..\dist"
)
$ErrorActionPreference='Stop'
& "$PSScriptRoot\build.ps1" -SdkExamples $SdkExamples -CudaPath $CudaPath -BuildDir $BuildDir -OutputDir $OutputDir
& cmd /d /c "$PSScriptRoot\verify.cmd"
if($LASTEXITCODE -ne 0){throw "Verification failed with exit code $LASTEXITCODE"}
