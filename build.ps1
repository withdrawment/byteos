$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Push-Location $root
try {
    function Find-Tool([string[]]$candidates, [string]$fallback) {
        foreach ($c in $candidates) {
            if ($c -and (Test-Path $c)) { return $c }
        }
        $cmd = Get-Command $fallback -ErrorAction SilentlyContinue
        if ($cmd) { return $cmd.Source }
        throw "Could not find $fallback. See README.md for toolchain requirements."
    }

    $nasm = Find-Tool @(
        (Join-Path $root 'tools\nasm\nasm-3.02\nasm.exe'),
        'C:\Program Files\NASM\nasm.exe'
    ) 'nasm'

    $clang = Find-Tool @(
        'C:\Program Files\LLVM\bin\clang.exe',
        (Join-Path $env:ProgramFiles 'LLVM\bin\clang.exe')
    ) 'clang'
    $llvmBin = Split-Path -Parent $clang
    $ld = Find-Tool @((Join-Path $llvmBin 'ld.lld.exe')) 'ld.lld'
    $objcopy = Find-Tool @((Join-Path $llvmBin 'llvm-objcopy.exe')) 'llvm-objcopy'

    $python = Find-Tool @(
        (Join-Path $env:LOCALAPPDATA 'Programs\Python\Python312\python.exe'),
        'C:\Python312\python.exe'
    ) 'python'

    New-Item -ItemType Directory -Force build | Out-Null

    $cflags = @(
        '--target=x86_64-unknown-none-elf',
        '-std=c11', '-O2', '-ffreestanding', '-fno-builtin',
        '-fno-stack-protector', '-fno-pic', '-fno-pie', '-mno-red-zone',
        '-fno-asynchronous-unwind-tables', '-fno-ident',
        '-Wall', '-Wextra', '-I', 'kernel', '-I', 'build'
    )

    Write-Host "  ASM  boot/mbr.asm"
    & $nasm -f bin "boot/mbr.asm" -o "build/mbr.bin"
    if ($LASTEXITCODE -ne 0) { throw "NASM failed on boot/mbr.asm" }
    & $python tools/bin2c.py build/mbr.bin build/mbr_blob.c g_mbr_image
    if ($LASTEXITCODE -ne 0) { throw "mbr embedding failed" }
    & $python tools/embed_site.py build
    if ($LASTEXITCODE -ne 0) { throw "website embedding failed" }

    $sources = @(
        'kernel/core.c', 'kernel/platform.c', 'kernel/ui.c', 'kernel/donut.c',
        'kernel/ata.c', 'kernel/bfs.c', 'kernel/auth.c', 'kernel/install.c',
        'kernel/fetch.c', 'kernel/fetch_art.c', 'kernel/desktop.c', 'kernel/browser.c',
        'kernel/editor.c', 'kernel/win95.c', 'kernel/tilewm.c',
        'build/site_data.c', 'build/mbr_blob.c'
    )
    $objects = @()
    foreach ($src in $sources) {
        $obj = 'build/' + [System.IO.Path]::GetFileNameWithoutExtension($src) + '.o'
        Write-Host "  CC   $src"
        & $clang @cflags -c $src -o $obj
        if ($LASTEXITCODE -ne 0) { throw "clang failed on $src" }
        $objects += $obj
    }

    Write-Host "  LD   build/kernel.elf"
    & $ld -T kernel/linker.ld -nostdlib -static -o build/kernel.elf @objects
    if ($LASTEXITCODE -ne 0) { throw "ld.lld failed" }

    & $objcopy -O binary build/kernel.elf build/kernel.bin
    if ($LASTEXITCODE -ne 0) { throw "llvm-objcopy failed" }

    $kernelSectors = [int][math]::Ceiling((Get-Item build/kernel.bin).Length / 512.0)
    Write-Host "  ASM  boot/boot.asm ($kernelSectors kernel sectors)"
    & $nasm -f bin -D KERNEL_SECTORS=$kernelSectors boot/boot.asm -o build/boot.bin
    if ($LASTEXITCODE -ne 0) { throw "NASM failed" }

    $bootSectors = [int][math]::Ceiling((Get-Item build/boot.bin).Length / 512.0)
    Write-Host "  ASM  boot/usbmbr.asm ($bootSectors boot image sectors)"
    & $nasm -f bin -D BOOT_IMAGE_SECTORS=$bootSectors boot/usbmbr.asm -o build/usbmbr.bin
    if ($LASTEXITCODE -ne 0) { throw "NASM failed on boot/usbmbr.asm" }

    $isoArgs = @('tools/mkiso.py', 'build/boot.bin', 'ByteOS.iso', '--volume-id', 'BYTEOS',
                 '--mbr', 'build/usbmbr.bin')
    if (Test-Path 'README.md') {
        $isoArgs += @('--readme', 'README.md')
    }
    Write-Host "  ISO  ByteOS.iso"
    & $python @isoArgs
    if ($LASTEXITCODE -ne 0) { throw "ISO build failed" }

    $size = & (Join-Path $llvmBin 'llvm-size.exe') build/kernel.elf
    Write-Host ''
    Write-Host "  $size"
    Get-Item build/boot.bin, build/kernel.bin, ByteOS.iso |
        Select-Object Name, Length |
        Format-Table -AutoSize
    Write-Host "Built: $root\ByteOS.iso"
}
finally {
    Pop-Location
}
