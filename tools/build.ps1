# build.ps1: build unowa .3dsx under devkitPro's MSYS2 shell.
#
#   pwsh tools/build.ps1                      # release + profile, both editions
#   pwsh tools/build.ps1 -Edition black -Profile 1 -Jobs 8
#   pwsh tools/build.ps1 -Clean
param(
    [ValidateSet('both', 'black', 'white')]
    [string]$Edition = 'both',
    [ValidateSet(0, 1)]
    [int]$Profile = 0,
    [int]$Jobs = 8,
    [switch]$Clean,
    [switch]$AllProfiles
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$bash = 'C:\devkitPro\msys2\usr\bin\bash.exe'
if (-not (Test-Path -LiteralPath $bash)) { throw "devkitPro MSYS2 bash not found at $bash" }
$unix = (& $bash -lc "cygpath -u '$repo'").Trim()

$editions = if ($Edition -eq 'both') { @(0, 1) } elseif ($Edition -eq 'black') { @(0) } else { @(1) }
$profiles = if ($AllProfiles) { @(0, 1) } else { @($Profile) }
foreach ($e in $editions) {
    foreach ($p in $profiles) {
        $cmd = if ($Clean) {
            'cd ' + "'" + $unix + "'" + " && make EDITION=$e PROFILE=$p clean"
        } else {
            'cd ' + "'" + $unix + "'" + " && sh tools/build.sh $e $p -j$Jobs"
        }
        & $bash -lc $cmd
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }
}
Get-ChildItem -Path (Join-Path $repo 'build') -Filter '*.3dsx' -Recurse |
    ForEach-Object { Write-Output $_.FullName }
