bits 16
org 0x7C00

BOOT_IMAGE_LBA equ 80
BOUNCE_SEG     equ 0x0080
STUB_SEG       equ 0x0060

%ifndef BOOT_IMAGE_SECTORS
%define BOOT_IMAGE_SECTORS 640
%endif

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

    mov ah, 0x41
    mov bx, 0x55AA
    mov dl, [drive]
    int 0x13
    jc fail
    cmp bx, 0xAA55
    jne fail

    mov word [dap_count], 1
    mov word [dap_off], 0
    mov word [dap_seg], BOUNCE_SEG
    mov dword [dap_lba], BOOT_IMAGE_LBA
    mov dword [dap_lba + 4], 0
    call read_dap

    mov ax, BOOT_IMAGE_SECTORS - 1
    mov [remaining], ax
    mov word [dap_off], 0
    mov word [dap_seg], 0x07E0
    mov dword [dap_lba], BOOT_IMAGE_LBA + 1
    mov dword [dap_lba + 4], 0

.loop:
    mov ax, [remaining]
    test ax, ax
    jz .prep
    mov bx, ax
    cmp bx, 64
    jbe .cap
    mov bx, 64
.cap:
    mov ax, [dap_seg]
    shl ax, 4
    mov cx, ax
    neg cx
    jz .use_count
    shr cx, 9
    cmp bx, cx
    jbe .use_count
    mov bx, cx
.use_count:
    mov [dap_count], bx
    call read_dap

    movzx eax, word [dap_count]
    sub [remaining], ax
    add dword [dap_lba], eax
    shl eax, 5
    add [dap_seg], ax
    jmp .loop

.prep:
    mov si, final_stub
    mov di, STUB_SEG * 16
    mov cx, final_stub_end - final_stub
    rep movsb
    mov dl, [drive]
    jmp 0x0000:STUB_SEG * 16

read_dap:
    mov si, dap
    mov ah, 0x42
    mov dl, [drive]
    int 0x13
    jc fail
    ret

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
    dw 1
dap_off:
    dw 0
dap_seg:
    dw BOUNCE_SEG
dap_lba:
    dq BOOT_IMAGE_LBA

remaining:
    dw BOOT_IMAGE_SECTORS - 1

drive:
    db 0
fail_msg:
    db "ByteOS: USB boot failed", 13, 10, 0

final_stub:
    mov ax, BOUNCE_SEG
    mov ds, ax
    xor ax, ax
    mov es, ax
    xor si, si
    mov di, 0x7C00
    mov cx, 256
    cld
    rep movsw
    jmp 0x0000:0x7C00
final_stub_end:

    times 446 - ($ - $$) db 0
    times 64 db 0
    dw 0xAA55
