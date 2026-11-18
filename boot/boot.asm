; ByteOS boot image
; BIOS El Torito loads this whole image at 0x7C00 (no emulation).
; Stage 1: set up segments and jump to stage 2.
; Stage 2: A20, E820, VGA text mode, page tables, long mode, jump to kernel.

bits 16
org 0x7C00

KERNEL_LOAD_ADDR equ 0x10000
BOOT_INFO_ADDR   equ 0x5000
E820_ENTRIES     equ 0x5008
BOOT_MAGIC       equ 0x42595445      ; "BYTE"
BOOT_DRIVE_OFS   equ 3080
BOOT_IMAGE_OFS   equ 3084
BOOT_FLAGS_OFS   equ 3088
DISK_FLAG_ADDR   equ 0x4FF0
DISK_FLAG_VALUE  equ 0x4449534B      ; "DISK"
BOOT_DRIVE_SAVE  equ 0x4FE0
KERNEL_START_LBA equ 0x8400 / 512

%ifndef KERNEL_SECTORS
%define KERNEL_SECTORS 128
%endif

PML4_ADDR        equ 0x1000
PDPT_ADDR        equ 0x2000
PD_ADDR          equ 0x3000
STACK16          equ 0x6000

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, STACK16
    cld
    mov dword [DISK_FLAG_ADDR], 0
    jmp 0x0000:stage2

    times 510 - ($ - $$) db 0
    dw 0xAA55

stage2:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax

    mov [BOOT_DRIVE_SAVE], dl
    xor eax, eax
    mov al, [BOOT_DRIVE_SAVE]
    mov [BOOT_INFO_ADDR + BOOT_DRIVE_OFS], eax
    mov dword [BOOT_INFO_ADDR + BOOT_IMAGE_OFS], boot_image_end - $$
    mov dword [BOOT_INFO_ADDR + BOOT_FLAGS_OFS], 0

    call enable_a20

    mov eax, [DISK_FLAG_ADDR]
    cmp eax, DISK_FLAG_VALUE
    jne .not_disk_boot
    mov dword [BOOT_INFO_ADDR + BOOT_FLAGS_OFS], 1
    call load_kernel_disk
.not_disk_boot:
    mov dword [DISK_FLAG_ADDR], 0

    call do_e820

    ; 80x25 colour text mode.
    mov ax, 0x0003
    int 0x10

    call build_page_tables

    ; Enable PAE, SSE state saving, and clear CR0.EM.
    mov eax, cr4
    or eax, (1 << 5) | (1 << 9) | (1 << 10)
    mov cr4, eax
    mov eax, cr0
    and eax, ~(1 << 2)
    or eax, (1 << 1)
    mov cr0, eax

    lgdt [gdt_desc]

    ; Enter 32-bit protected mode first.
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:pm32

; ---------------------------------------------------------------- A20
enable_a20:
    ; Fast A20 gate.
    in al, 0x92
    or al, 0x02
    and al, 0xFE
    out 0x92, al

    ; Keyboard-controller A20 gate, for older machines.
    call a20_wait
    mov al, 0xAD
    out 0x64, al
    call a20_wait
    mov al, 0xD0
    out 0x64, al
    call a20_wait
    in al, 0x60
    push ax
    call a20_wait
    mov al, 0xD1
    out 0x64, al
    call a20_wait
    pop ax
    or al, 0x02
    out 0x60, al
    call a20_wait
    mov al, 0xAE
    out 0x64, al
    call a20_wait
    ret

a20_wait:
    in al, 0x64
    test al, 0x02
    jnz a20_wait
    ret

; ---------------------------------------------------------------- E820
do_e820:
    xor ax, ax
    mov es, ax
    mov dword [BOOT_INFO_ADDR], BOOT_MAGIC
    mov dword [BOOT_INFO_ADDR + 4], 0
    mov di, E820_ENTRIES
    xor ebx, ebx

.loop:
    cmp word [BOOT_INFO_ADDR + 4], 128
    jae .done

    mov eax, 0xE820
    mov edx, 0x534D4150
    mov ecx, 24
    push di
    int 0x15
    pop di
    jc .done
    cmp eax, 0x534D4150
    jne .done
    cmp ecx, 20
    jb .done

    mov dword [di + 20], 0
    inc word [BOOT_INFO_ADDR + 4]
    add di, 24

    test ebx, ebx
    jnz .loop

.done:
    ret

; ---------------------------------------------------- Disk kernel loader
; When booted from a hard disk the MBR only loads stage 2.  Pull the flat
; kernel from LBA 66 (byte offset 0x8400 in the boot image) into 0x10000.
load_kernel_disk:
    mov word [disk_dap_count], 64
    mov word [disk_dap_off], 0
    mov word [disk_dap_seg], 0x1000
    mov dword [disk_dap_lba], KERNEL_START_LBA
    mov dword [disk_dap_lba + 4], 0
    mov word [kernel_remaining], KERNEL_SECTORS

.loop:
    mov ax, [kernel_remaining]
    test ax, ax
    jz .done
    cmp ax, 64
    jbe .have_count
    mov ax, 64
.have_count:
    mov [disk_dap_count], ax
    mov si, disk_dap
    mov ah, 0x42
    mov dl, [BOOT_DRIVE_SAVE]
    int 0x13
    jc disk_error

    movzx eax, word [disk_dap_count]
    sub [kernel_remaining], ax
    add dword [disk_dap_lba], eax
    shl eax, 5
    add [disk_dap_seg], ax
    jmp .loop

.done:
    ret

disk_error:
    mov si, disk_error_msg
.print:
    lodsb
    test al, al
    jz .hang
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    jmp .print
.hang:
    cli
    hlt
    jmp .hang

align 4
disk_dap:
    db 0x10, 0
disk_dap_count:
    dw 1
disk_dap_off:
    dw 0
disk_dap_seg:
    dw 0x1000
disk_dap_lba:
    dq KERNEL_START_LBA

kernel_remaining:
    dw 1

disk_error_msg:
    db "ByteOS: disk kernel load failed", 13, 10, 0

; ---------------------------------------------------------- Page tables
build_page_tables:
    ; Zero PML4, PDPT and the single PD.
    xor eax, eax
    mov edi, PML4_ADDR
    mov ecx, 0x3000 / 4
    rep stosd

    ; PML4[0] -> PDPT, present + writable.
    mov dword [PML4_ADDR], PDPT_ADDR | 0x03
    mov dword [PML4_ADDR + 4], 0

    ; PDPT[0] -> PD, present + writable.
    mov dword [PDPT_ADDR], PD_ADDR | 0x03
    mov dword [PDPT_ADDR + 4], 0

    ; 512 x 2 MiB identity pages = first 1 GiB.
    mov edi, PD_ADDR
    mov eax, 0x00000083              ; present, writable, 2 MiB page
    mov ecx, 512
.fill_pd:
    mov [edi], eax
    mov dword [edi + 4], 0
    add eax, 0x200000
    add edi, 8
    loop .fill_pd
    ret

; ---------------------------------------------------------------- GDT
align 8
gdt:
    dq 0x0000000000000000            ; 0x00 null
    dq 0x00CF9A000000FFFF            ; 0x08 32-bit code, flat 4 GiB
    dq 0x00CF92000000FFFF            ; 0x10 data, flat 4 GiB
    dq 0x00AF9A000000FFFF            ; 0x18 64-bit code
gdt_end:

gdt_desc:
    dw gdt_end - gdt - 1
    dd gdt

; ------------------------------------------------------- 32-bit trampoline
bits 32
pm32:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x7000

    ; Long Mode Enable.
    mov ecx, 0xC0000080
    rdmsr
    or eax, 0x00000100
    wrmsr

    mov eax, PML4_ADDR
    mov cr3, eax

    ; Paging on. We are now in long mode compatibility mode.
    mov eax, cr0
    or eax, 0x80000000
    mov cr0, eax
    jmp 0x18:long_mode_entry

; ------------------------------------------------------------- 64-bit entry
bits 64
long_mode_entry:
    cld
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov rsp, 0x90000
    xor rbp, rbp
    fninit

    mov rdi, BOOT_INFO_ADDR
    mov rax, KERNEL_LOAD_ADDR
    call rax

.hang:
    cli
    hlt
    jmp .hang

; ---------------------------------------------------------------- Kernel
; Place the flat kernel binary at physical 0x10000.
    times 0x8400 - ($ - $$) db 0
    incbin "build/kernel.bin"
boot_image_end:
