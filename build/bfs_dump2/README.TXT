# ByteOS 0.1 "Donut"

A tiny 64-bit bare-metal kernel for x86_64. It boots from a hand-built
El Torito ISO, enters long mode, talks directly to VGA text memory and a
PS/2 keyboard, reports live CPU/memory/RTC information, and renders a
spinning ASCII donut.

No GRUB. No libc. No operating system underneath.

![ByteOS boot screen](screenshots/home.png)

![ByteOS spinning donut](screenshots/donut.png)

![ByteOS system report](screenshots/specs.png)

The screenshots above are from a QEMU run with 1 GiB RAM and four vCPUs.

## Try it

```powershell
.\build.ps1
.\run.ps1
```

`build.ps1` produces `ByteOS.iso` in the project root. `run.ps1` starts it
in QEMU with 1 GiB of RAM and four virtual CPU cores.

The ISO is a legacy BIOS / CSM boot image. It works in QEMU, VirtualBox
(BIOS mode), VMware, on a burned CD/DVD, and from Ventoy. A UEFI-only
machine needs CSM enabled; native UEFI boot is future work.

## Shell commands

| Command | What it does |
| --- | --- |
| `help` | Command list |
| `specs` | CPUID, memory map, TSC, RTC and build report |
| `memmap` | Raw BIOS E820 memory map |
| `about` | What ByteOS is made of |
| `donut` | Spin the ASCII donut; any key returns |
| `logo` | Redraw the boot screen |
| `clear` | Clear the screen |
| `reboot` | Warm reboot |

Commands are case-insensitive. `sysinfo` aliases `specs`; `cls` aliases
`clear`; `?` aliases `help`; `home` aliases `logo`.

## What is in the box

```
boot/boot.asm     16-bit BIOS loader + 32-bit trampoline + long-mode entry
kernel/core.c     entry point, VGA console, kprintf, string/memory helpers
kernel/platform.c CPUID, E820, RTC/CMOS, PIT/TSC calibration, PS/2 keyboard
kernel/ui.c       boot screen, system report, help/about/memmap, shell
kernel/donut.c    z-buffered spinning ASCII torus
kernel/sin_table.h generated Q15 sine table
tools/mkiso.py    ISO9660 + El Torito ISO writer (no external ISO tools)
tools/gen_sin.py  regenerates the sine table
build.ps1         build everything and produce ByteOS.iso
run.ps1           boot the ISO in QEMU
```

## Boot path

1. BIOS finds the El Torito boot catalog and loads the 512-byte-sector
   boot image at physical `0x7C00`.
2. Stage 2 enables A20, collects the E820 memory map, switches to VGA
   text mode 80x25, and identity-maps the first 1 GiB with 2 MiB pages.
3. It enables PAE, long mode and SSE state saving, then jumps to the flat
   kernel at `0x10000` on a stack at `0x90000`.
4. The kernel zeroes `.bss`, detects the CPU, reads the memory map, and
   calibrates the TSC with PIT channel 2 before starting the shell.

The boot image is padded so the kernel lands exactly at `0x10000`. The
ISO writer lays out the Primary Volume Descriptor, Boot Record Volume
Descriptor, terminator, boot catalog, boot image, root directory, path
tables, and the ISO copy of this README. SeaBIOS/QEMU is strict about the
Boot Record Volume Descriptor being at LBA 17 and the
`EL TORITO SPECIFICATION` field being zero-padded; the builder follows
that layout.

## Build requirements

- NASM (tested with 3.02)
- LLVM/Clang with `ld.lld` and `llvm-objcopy` (tested with 23.1.6)
- Python 3 (tested with 3.12)
- QEMU only for `run.ps1` (tested with 11.1.0)

On Windows these can be installed with:

```powershell
winget install NASM.NASM
winget install LLVM.LLVM
winget install Python.Python.3.12
winget install SoftwareFreedomConservancy.QEMU
```

The build script finds the usual installation paths, and also checks
`tools/nasm/nasm-3.02/nasm.exe` for a portable NASM copy.

## Art

The boot screen uses a custom donut mascot and a 5x5 block-letter BYTEOS
wordmark, drawn straight into VGA text memory. The donut command is a
classic perspective-projected torus with a z-buffer and a 12-character
luminance ramp, animated at roughly 30 frames per second using the
calibrated TSC.

## Notes / next steps

This is a kernel, not yet a full OS. Natural next steps are an IDT and
PIC/APIC timer interrupts, physical page allocation, a heap, a real
filesystem, UEFI boot, and user mode. For now it deliberately stays
small enough to read end-to-end.

The system report is live: on real hardware it shows the real CPU brand,
CPU flags, memory map, TSC estimate, and CMOS clock. Under QEMU it shows
the virtual hardware and a "VM" marker on the boot screen.
