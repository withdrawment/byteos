$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$iso = Join-Path $root 'ByteOS.iso'

if (-not (Test-Path $iso)) {
    throw "ByteOS.iso not found. Run .\build.ps1 first."
}

$candidates = @(
    'C:\Program Files\qemu\qemu-system-x86_64.exe',
    (Join-Path $env:ProgramFiles 'qemu\qemu-system-x86_64.exe')
)
$qemu = $null
foreach ($c in $candidates) {
    if ($c -and (Test-Path $c)) { $qemu = $c; break }
}
if (-not $qemu) {
    $cmd = Get-Command qemu-system-x86_64 -ErrorAction SilentlyContinue
    if ($cmd) { $qemu = $cmd.Source }
}
if (-not $qemu) {
    throw "QEMU not found. Install it with: winget install SoftwareFreedomConservancy.QEMU"
}

Write-Host "Booting ByteOS 0.2 in QEMU (1 GiB RAM, 4 CPUs)..."
& $qemu `
    -name "ByteOS 0.2" `
    -m 1024 `
    -smp 4,sockets=1,cores=4,threads=1 `
    -cdrom $iso `
    -boot d `
    -vga std `
    -nic none
