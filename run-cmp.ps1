$ErrorActionPreference = 'Stop'
$qtBin = 'F:\Apps\Qt\6.9.3\mingw_64\bin'
$compilerBin = 'F:\Apps\Qt\Tools\mingw1310_64\bin'
$env:PATH = "$qtBin;$compilerBin;$env:PATH"
$env:QT_QPA_PLATFORM = 'windows'
& (Join-Path $PSScriptRoot 'build\cmp.exe')
