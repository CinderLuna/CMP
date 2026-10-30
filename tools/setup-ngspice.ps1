$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$downloads = Join-Path $projectRoot 'work/downloads'
New-Item -ItemType Directory -Force $downloads | Out-Null
$packages = @(
    @{ Name='ngspice-47_64.7z'; Hash='59225971BD68CDD1199443649AA4615A9E6D684933F205AB49006A3942518F5A'; Directory='third_party/ngspice' },
    @{ Name='ngspice-47_dll_64.7z'; Hash='5BF5EEC7DB989F74849CDB32F7DF62693F4F149CCEEEBCBD222EB35C2BB821EB'; Directory='third_party/ngspice-shared' }
)
foreach ($package in $packages) {
    $archive = Join-Path $downloads $package.Name
    if (!(Test-Path -LiteralPath $archive)) {
        & curl.exe -L --fail --retry 1 "https://downloads.sourceforge.net/project/ngspice/ng-spice-rework/47/$($package.Name)" -o $archive
        if ($LASTEXITCODE -ne 0) { throw "Download failed: $($package.Name)" }
    }
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $package.Hash) { throw "Archive checksum mismatch: $archive" }
    $destination = Join-Path $projectRoot $package.Directory
    New-Item -ItemType Directory -Force $destination | Out-Null
    & tar.exe -xf $archive -C $destination
    # Windows bsdtar may warn about unrelated unnamed entries in this upstream archive.
    # Verify each required artifact below instead of assuming extraction succeeded.
}
$shared = Join-Path $projectRoot 'third_party/ngspice-shared/Spice64_dll/dll-vs'
$console = Join-Path $projectRoot 'third_party/ngspice/Spice64/bin'
foreach ($name in @('sndfile.dll','samplerate.dll')) {
    Copy-Item -LiteralPath (Join-Path $console $name) -Destination (Join-Path $shared $name)
}
foreach ($name in @('ngspice.dll','libomp140.x86_64.dll','sndfile.dll','samplerate.dll')) {
    if (!(Test-Path -LiteralPath (Join-Path $shared $name))) { throw "Missing runtime: $name" }
}
if (!(Test-Path -LiteralPath (Join-Path $projectRoot 'third_party/ngspice-shared/Spice64_dll/include/ngspice/sharedspice.h'))) { throw 'Missing sharedspice.h' }
Write-Host 'ngspice 47 runtime and header are ready.'
