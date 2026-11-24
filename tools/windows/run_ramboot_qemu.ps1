# SPDX-License-Identifier: MIT
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string]$Release,
    [Parameter(Mandatory)] [string]$Kernel,
    [Parameter(Mandatory)] [string]$Qemu,
    [Parameter(Mandatory)] [string]$QemuShare,
    [string]$Repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [string]$Python = 'python',
    [int]$MemoryGB = 10,
    [int]$Cpus = 4,
    [int]$Seconds = 1200,
    [string]$WorkDir = '',
    [string]$BootArgs = 'loader_return_after=1200 epoch=0 virtio_input=1 cpus=4 gpu=0 metal=0 wifi=0 adsp=0 ufs=0 ufs_native=0 ufs_write=0 var_store=0 touchscreen=1 launchd_unsecure_cache=1 apfs_volume=md0,v=1 apfs_volume_at=/private/preboot'
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
$QemuShare = (Resolve-Path -LiteralPath $QemuShare).Path
$mkpayload = Need-File (Join-Path $Repo 'arch\arm64\boot\mkpayload.py') 'mkpayload.py'
$system = Need-File (Join-Path $Release 'system27-prune.apfs') 'system image'
$root = Need-File (Join-Path $Release 'root26-darwin.apfs') 'root image'
$loader = Need-File (Join-Path $Release 'loader.efi') 'loader'
$bios = Need-File (Join-Path $QemuShare 'edk2-aarch64-code.fd') 'edk2-aarch64-code.fd'
$varsTemplate = Need-File (Join-Path $QemuShare 'edk2-arm-vars.fd') 'edk2-arm-vars.fd'

$qemuPath = (Get-Command $Qemu -ErrorAction SilentlyContinue).Source
if (-not $qemuPath) {
    $qemuPath = $Qemu
    if (-not (Test-Path -LiteralPath $qemuPath -PathType Leaf)) {
        throw "missing QEMU executable: $Qemu"
    }
}
$qemuPath = (Resolve-Path -LiteralPath $qemuPath).Path

if ($Seconds -lt 30) { throw 'Seconds must be at least 30' }
if ($MemoryGB -lt 4) { throw 'MemoryGB must be at least 4' }
if (-not $WorkDir) {
    $WorkDir = Join-Path ([IO.Path]::GetTempPath()) ('hakios-qemu-' + [guid]::NewGuid().ToString('N'))
}
$WorkDir = [IO.Path]::GetFullPath($WorkDir)
New-Item -ItemType Directory -Force -Path $WorkDir | Out-Null
$esp = Join-Path $WorkDir 'esp'
$boot = Join-Path $esp 'EFI\BOOT'
New-Item -ItemType Directory -Force -Path $boot | Out-Null
Copy-Item -LiteralPath $loader -Destination (Join-Path $boot 'BOOTAA64.EFI') -Force

$vars = Join-Path $WorkDir 'vars.fd'
Copy-Item -LiteralPath $varsTemplate -Destination $vars -Force
$payload = Join-Path $WorkDir 'payload.bin'
$log = Join-Path $WorkDir 'qemu-serial.log'
$err = Join-Path $WorkDir 'qemu-stderr.log'
Remove-Item -LiteralPath $log, $err -Force -ErrorAction SilentlyContinue

$sysSize = (Get-Item -LiteralPath $system).Length
$rdres = ('{0:x}' -f $sysSize)
$epoch = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds()
$cmdline = $BootArgs.Replace('epoch=0', "epoch=$epoch").Replace('loader_return_after=1200', "loader_return_after=$Seconds")
& $Python $mkpayload -o $payload "kernel=$Kernel" "ramdisk=$root" --rdres $rdres --cmdline $cmdline
if ($LASTEXITCODE -ne 0) { throw "mkpayload failed: $LASTEXITCODE" }

$qargs = @(
    '-M', 'virt,gic-version=3',
    '-cpu', 'cortex-a76',
    '-smp', "$Cpus",
    '-m', "${MemoryGB}G",
    '-accel', 'tcg,tb-size=384,thread=multi',
    '-drive', "if=pflash,format=raw,readonly=on,file=$bios",
    '-drive', "if=pflash,format=raw,file=$vars",
    '-drive', "file=fat:rw:$esp,format=raw,if=virtio",
    '-device', "loader,file=$system,addr=0xFC000000,force-raw=on",
    '-device', "loader,file=$payload,addr=0xF0000000,force-raw=on",
    '-global', 'virtio-mmio.force-legacy=false',
    '-device', 'virtio-tablet-device',
    '-device', 'virtio-keyboard-device',
    '-device', 'virtio-rng-pci',
    '-device', 'ramfb',
    '-display', 'none',
    '-serial', 'stdio',
    '-monitor', 'none',
    '-no-reboot'
)

Write-Host "starting QEMU for up to $Seconds s; serial log: $log"
$process = Start-Process -FilePath $qemuPath -ArgumentList $qargs -WorkingDirectory $WorkDir -PassThru -WindowStyle Hidden -RedirectStandardOutput $log -RedirectStandardError $err
$started = Get-Date
$sawLaunchd = $false
$sawSpringBoard = $false
$sawPanic = $false
while (-not $process.HasExited -and ((Get-Date) - $started).TotalSeconds -lt ($Seconds + 30)) {
    Start-Sleep -Seconds 2
    if (Test-Path -LiteralPath $log) {
        $text = Get-Content -LiteralPath $log -Raw -ErrorAction SilentlyContinue
        if ($text -match 'launchd\[') { $sawLaunchd = $true }
        if ($text -match 'SpringBoard\[') { $sawSpringBoard = $true; Start-Sleep -Seconds 5; break }
        if ($text -match 'UEFI EXCEPTION|Kernel panic|panic:') { $sawPanic = $true; Start-Sleep -Seconds 2; break }
    }
}
$alive = Get-Process -Id $process.Id -ErrorAction SilentlyContinue
if ($alive -and -not $process.HasExited) { Stop-Process -Id $process.Id -Force }
Write-Host "qemu: launchd=$sawLaunchd springboard=$sawSpringBoard panic=$sawPanic workdir=$WorkDir"
if (Test-Path -LiteralPath $log) { Get-Content -LiteralPath $log -Tail 120 }
if ($sawPanic -or -not $sawSpringBoard) { exit 1 }
