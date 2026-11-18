; ByteOS disk MBR
; Loads the stage-2 image from LBA 1 to 0x7E00, then jumps to it.
; The partition table is filled in by the ByteOS installer.

bits 16
org 0x7C00

STAGE2_ADDR     equ 0x7E00
STAGE2_SECTORS  equ 65
DISK_FLAG_ADDR  equ 0x4FF0
DISK_FLAG_VALUE equ 0x4449534B

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    cld

    mov [drive], dl
    mov dword [DISK_FLAG_ADDR], DISK_FLAG_VALUE

    mov ah, 0x41
    mov bx, 0x55AA
    mov dl, [drive]
    int 0x13
    jc fail
    cmp bx, 0xAA55
    jne fail

    mov word [dap_count], STAGE2_SECTORS
    mov word [dap_off], STAGE2_ADDR
    mov word [dap_seg], 0
    mov dword [dap_lba], 1
    mov dword [dap_lba + 4], 0

    mov si, dap
    mov ah, 0x42
    mov dl, [drive]
    int 0x13
    jc fail

    mov dl, [drive]
    jmp 0x0000:STAGE2_ADDR

fail:
    mov si, fail_msg
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
dap:
    db 0x10, 0
dap_count:
    dw STAGE2_SECTORS
dap_off:
    dw STAGE2_ADDR
dap_seg:
    dw 0
dap_lba:
    dq 1

drive:
    db 0
fail_msg:
    db "ByteOS: disk boot failed", 13, 10, 0

    times 446 - ($ - $$) db 0

; Partition table (patched by byte-install).
    times 64 db 0

    dw 0xAA55
