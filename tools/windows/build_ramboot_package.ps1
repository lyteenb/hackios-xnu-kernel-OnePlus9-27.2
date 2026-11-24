# SPDX-License-Identifier: MIT
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string]$Release,
    [Parameter(Mandatory)] [string]$Kernel,
    [Parameter(Mandatory)] [string]$Uefi,
    [Parameter(Mandatory)] [string]$Out,
    [Parameter(Mandatory)] [string]$HardwareSerialSha256,
    [string]$Repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [string]$Python = 'python',
    [string]$Defconfig = 'phone_27_2_perf'
)

$ErrorActionPreference = 'Stop'

function Need-File([string]$Path, [string]$What) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "missing ${What}: $Path"
    }
    return (Resolve-Path -LiteralPath $Path).Path
}

$Repo = (Resolve-Path -LiteralPath $Repo).Path
$Release = (Resolve-Path -LiteralPath $Release).Path
$Kernel = Need-File $Kernel 'kernel'
$Uefi = (Resolve-Path -LiteralPath $Uefi).Path
$system = Need-File (Join-Path $Release 'system27-prune.apfs') 'system image'
$root = Need-File (Join-Path $Release 'root26-darwin.apfs') 'root image'
$loader = Need-File (Join-Path $Release 'loader.efi') 'loader'
$mkpayload = Need-File (Join-Path $Repo 'arch\arm64\boot\mkpayload.py') 'mkpayload.py'
$mkboot = Need-File (Join-Path $Repo 'arch\arm64\boot\mkboot.py') 'mkboot.py'
$imagePy = Need-File (Join-Path $Repo 'tools\image.py') 'tools/image.py'
$launcher = Need-File (Join-Path $Repo 'tools\windows\launch_ramboot.py') 'launch_ramboot.py'
$Uefi = (Resolve-Path -LiteralPath $Uefi).Path

if ($HardwareSerialSha256 -notmatch '^[0-9a-fA-F]{64}$') {
    throw 'HardwareSerialSha256 must be a 64-character hexadecimal SHA-256 digest'
}
$HardwareSerialSha256 = $HardwareSerialSha256.ToLowerInvariant()

$Out = [IO.Path]::GetFullPath($Out)
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$imageDir = Join-Path $Out 'image'
New-Item -ItemType Directory -Force -Path $imageDir | Out-Null

Write-Host "building loader image with defconfig $Defconfig"
& $Python $imagePy build --board lemonade --defconfig $Defconfig --kernel $Kernel --loader $loader --uefi $Uefi --out $imageDir
if ($LASTEXITCODE -ne 0) { throw "tools/image.py failed: $LASTEXITCODE" }
$imageManifestPath = Join-Path $imageDir 'manifest.json'
$imageManifest = Get-Content -LiteralPath $imageManifestPath -Raw | ConvertFrom-Json
$bootArguments = @($imageManifest.cmdline -split '\s+' | Where-Object { $_ })
if ($bootArguments.Count -eq 0) { throw 'image manifest has no boot arguments' }

$bootPayload = Join-Path $imageDir 'loader-only.payload'
& $Python $mkpayload -o $bootPayload "loader.efi=$loader" --usb
if ($LASTEXITCODE -ne 0) { throw "mkpayload failed: $LASTEXITCODE" }
$bootImage = Join-Path $Out 'boot.img'
& $Python $mkboot --board lemonade --uefi $Uefi -p $bootPayload -o $bootImage
if ($LASTEXITCODE -ne 0) { throw "mkboot failed: $LASTEXITCODE" }

Copy-Item -LiteralPath $Kernel -Destination (Join-Path $Out 'kernel') -Force
Copy-Item -LiteralPath $root -Destination (Join-Path $Out 'root.apfs') -Force
Copy-Item -LiteralPath $system -Destination (Join-Path $Out 'system.apfs') -Force
Copy-Item -LiteralPath $mkpayload -Destination (Join-Path $Out 'mkpayload.py') -Force
Copy-Item -LiteralPath $launcher -Destination (Join-Path $Out 'launch.py') -Force

$startLines = @(
    '$python = (Get-Command python -ErrorAction Stop).Source',
    "& `$python (Join-Path `$PSScriptRoot 'launch.py') start"
)
$returnLines = @(
    '$python = (Get-Command python -ErrorAction Stop).Source',
    "& `$python (Join-Path `$PSScriptRoot 'launch.py') android"
)
[IO.File]::WriteAllText((Join-Path $Out 'Start.ps1'), (($startLines -join "`n") + "`n"), (New-Object Text.UTF8Encoding($false)))
[IO.File]::WriteAllText((Join-Path $Out 'Return-to-Android.ps1'), (($returnLines -join "`n") + "`n"), (New-Object Text.UTF8Encoding($false)))

$readme = @(
    '# OnePlus 9 Darwin 27.2 RAM boot package',
    '',
    'This package contains Apple-derived system files and the phone UEFI files. Do not publish or redistribute it.',
    '',
    '```powershell',
    '.\Start.ps1',
    '.\Return-to-Android.ps1',
    'python .\launch.py verify',
    'python .\launch.py status',
    'python .\launch.py log',
    '```',
    '',
    'This package boots from RAM only and does not write Android partitions.'
) -join "`n"
[IO.File]::WriteAllText((Join-Path $Out 'README.md'), $readme, (New-Object Text.UTF8Encoding($false)))

$files = @('boot.img', 'kernel', 'root.apfs', 'system.apfs', 'mkpayload.py', 'launch.py')
$fileEntries = [ordered]@{}
foreach ($name in $files) {
    $path = Join-Path $Out $name
    $item = Get-Item -LiteralPath $path
    $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    $fileEntries[$name] = [ordered]@{ sha256 = $hash; bytes = $item.Length }
}

$sourceCommit = ''
try { $sourceCommit = (git -C $Repo rev-parse HEAD).Trim() } catch { }
$manifest = [ordered]@{
    profile = 'lemonade-ramboot-v1'
    version = 'lemonade-ramboot-' + (Get-Date -Format 'yyyyMMdd-HHmmss')
    created_at = [DateTimeOffset]::UtcNow.ToString('o')
    target = [ordered]@{ manufacturer = 'OnePlus'; model = 'LE2110'; device = 'OnePlus9'; soc = 'SM8350' }
    hardware_serial_sha256 = $HardwareSerialSha256
    boot_arguments = $bootArguments
    files = $fileEntries
    notes = 'RAM boot only; private Apple-derived system image; no partition writes.'
    source_commit = $sourceCommit
}
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $Out 'manifest.json') -Encoding utf8NoBOM

$outFull = [IO.Path]::GetFullPath($Out).TrimEnd('\') + '\'
$imageFull = [IO.Path]::GetFullPath($imageDir)
if (-not $imageFull.StartsWith($outFull, [StringComparison]::OrdinalIgnoreCase)) {
    throw "refusing to remove outside output directory: $imageFull"
}
Remove-Item -LiteralPath $imageFull -Recurse -Force

$all = Get-ChildItem -LiteralPath $Out -File | Sort-Object Name
$lines = foreach ($file in $all) {
    "$((Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant())  $($file.Name)"
}
$lines | Set-Content -LiteralPath (Join-Path $Out 'SHA256SUMS') -Encoding ascii
Get-ChildItem -LiteralPath $Out | Select-Object Name, Length
Write-Host "package: $Out"
