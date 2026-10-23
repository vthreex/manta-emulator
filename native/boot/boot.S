# Bare-metal BIOS boot code (El Torito no-emulation, 4 x 512-byte sectors loaded at 0x7C00).
# 1. VESA: find a 1024x768x32 linear-framebuffer mode and set it; 2. copy PAYLOAD.BIN from the CD (int 13h ext read, 2048-byte sectors) to 1 MB
# through unreal mode; 3. enable A20, identity-map 4 GB, enter long mode, jump to 0x100000.
# Boot info for the payload at 0x500: u32 fb, pitch, width, height, bpp, ok.  mkbare.py patches payload LBA/size at offset 8/12.
.intel_syntax noprefix
.code16
.globl _start
_start:
    jmp start
    .org 8
payload_lba:  .long 0
payload_size: .long 0
bootdrv:      .byte 0
start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x9000
    sti
    mov [bootdrv], dl
    # ---- VESA ----
    mov dword ptr [0x500], 0
    mov dword ptr [0x514], 0
    mov dword ptr [0x600], 0x32454256        # "VBE2"
    mov di, 0x600
    mov ax, 0x4F00
    int 0x10
    cmp ax, 0x004F
    jne novbe
    mov si, [0x600 + 14]                     # mode list far pointer
    mov ax, [0x600 + 16]
    mov fs, ax
modeloop:
    mov cx, fs:[si]
    add si, 2
    cmp cx, 0xFFFF
    je novbe
    mov di, 0x800
    mov ax, 0x4F01
    int 0x10
    cmp ax, 0x004F
    jne modeloop
    mov ax, [0x800]
    test ax, 0x80                            # linear framebuffer supported
    jz modeloop
    cmp word ptr [0x800 + 18], 1024
    jne modeloop
    cmp word ptr [0x800 + 20], 768
    jne modeloop
    cmp byte ptr [0x800 + 25], 32
    jne modeloop
    mov bx, cx
    or bx, 0x4000
    mov ax, 0x4F02
    int 0x10
    cmp ax, 0x004F
    jne novbe
    mov eax, [0x800 + 40]
    mov [0x500], eax
    movzx eax, word ptr [0x800 + 16]
    mov [0x504], eax
    movzx eax, word ptr [0x800 + 18]
    mov [0x508], eax
    movzx eax, word ptr [0x800 + 20]
    mov [0x50C], eax
    movzx eax, byte ptr [0x800 + 25]
    mov [0x510], eax
    mov dword ptr [0x514], 1
novbe:
    xor ax, ax
    mov fs, ax
    # ---- unreal mode ----
    cli
    lgdt [gdtr]
    mov eax, cr0
    or al, 1
    mov cr0, eax
    mov bx, 0x10
    mov fs, bx
    and al, 0xFE
    mov cr0, eax                              # back to real mode; fs keeps its 4 GB limit
    xor ax, ax
    mov ds, ax
    mov es, ax
    sti
    # ---- copy payload ----
    mov dword ptr [dest], 0x100000
    mov eax, [payload_lba]
    mov [dap_lba], eax
    mov ecx, [payload_size]
copyloop:
    test ecx, ecx
    jle copied
    push ecx
    mov word ptr [dap_off], 0
    mov word ptr [dap_seg], 0x1000
    mov word ptr [dap_cnt], 16
    mov si, offset dap
    mov dl, [bootdrv]
    mov ah, 0x42
    int 0x13
    jc diskerr
    pop ecx
    cli
    mov esi, 0x10000
    mov edi, [dest]
    mov ebx, 8192
cp:
    mov eax, [esi]
    mov fs:[edi], eax
    add esi, 4
    add edi, 4
    dec ebx
    jnz cp
    sti
    mov [dest], edi
    add dword ptr [dap_lba], 16
    sub ecx, 32768
    jmp copyloop
diskerr:
    hlt
    jmp diskerr
copied:
    # ---- long mode ----
    cli
    in al, 0x92
    or al, 2
    out 0x92, al
    xor eax, eax
    mov edi, 0x1000
    mov ecx, 0x6000 / 4
    rep stosd
    mov dword ptr [0x1000], 0x2003           # PML4[0] -> PDPT
    mov dword ptr [0x2000], 0x3003           # PDPT[0..3] -> PD0..PD3
    mov dword ptr [0x2008], 0x4003
    mov dword ptr [0x2010], 0x5003
    mov dword ptr [0x2018], 0x6003
    mov edi, 0x3000
    mov eax, 0x83                            # 2 MB pages, present|rw|ps
    mov ecx, 2048
fillpd:
    mov [edi], eax
    mov dword ptr [edi + 4], 0
    add eax, 0x200000
    add edi, 8
    dec ecx
    jnz fillpd
    mov eax, 0x1000
    mov cr3, eax
    mov eax, cr4
    or eax, 0x20
    mov cr4, eax
    mov ecx, 0xC0000080
    rdmsr
    or eax, 0x100
    wrmsr
    mov eax, cr0
    or eax, 0x80000001
    mov cr0, eax
    .byte 0x66, 0xEA
    .long long_entry
    .word 0x08
.code64
long_entry:
    xor eax, eax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    mov rax, cr0
    and eax, 0xFFFFFFFB                      # clear EM
    or eax, 2                                # MP
    mov cr0, rax
    mov rax, cr4
    or eax, 0x600                            # OSFXSR | OSXMMEXCPT
    mov cr4, rax
    mov rsp, 0x80000
    mov rax, 0x100000
    jmp rax
.align 8
gdt:
    .quad 0
    .quad 0x00AF9A000000FFFF                 # 0x08: 64-bit code
    .quad 0x00CF92000000FFFF                 # 0x10: flat data
gdtr:
    .word gdtr - gdt - 1
    .long gdt
dap:
    .byte 16, 0
dap_cnt: .word 0
dap_off: .word 0
dap_seg: .word 0
dap_lba: .quad 0
dest: .long 0
