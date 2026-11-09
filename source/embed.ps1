param([string]$BuildDir)
$ErrorActionPreference='Stop'
$bytes=[IO.File]::ReadAllBytes("$BuildDir\kernel.fatbin")
$text='alignas(8) static const unsigned char kKernelData[] = {'+($bytes -join ',')+'};'
[IO.File]::WriteAllText("$BuildDir\KernelData.h",$text)
