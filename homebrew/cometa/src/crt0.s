@ Cometa: startup code and cartridge header.
@ The header leaves the logo area empty: the game is meant for emulators, and
@ shipping that bitmap is neither needed nor ours to ship.

    .section .crt0, "ax"
    .arm
    .global _start
    .type   _start, %function
_start:
    b       rom_start
    .fill   156, 1, 0               @ 0x04 logo area (empty on purpose)
    .ascii  "COMETA\0\0\0\0\0\0"    @ 0xA0 title, 12 bytes
    .ascii  "CMTA"                  @ 0xAC game code
    .ascii  "ND"                    @ 0xB0 maker code
    .byte   0x96                    @ 0xB2 fixed value
    .byte   0x00                    @ 0xB3 unit code
    .byte   0x00                    @ 0xB4 device type
    .fill   7, 1, 0                 @ 0xB5 reserved
    .byte   0x00                    @ 0xBC version
    .byte   0x00                    @ 0xBD header checksum (patched by tools/fix_header.py)
    .fill   2, 1, 0                 @ 0xBE reserved

rom_start:
    @ IRQ mode stack, then system mode stack (the game runs in system mode).
    mov     r0, #0x12
    msr     cpsr_c, r0
    ldr     sp, =0x03007FA0
    mov     r0, #0x1F
    msr     cpsr_c, r0
    ldr     sp, =0x03007F00

    @ Copy initialized data and IWRAM code from ROM.
    ldr     r0, =__iwram_lma
    ldr     r1, =__iwram_start
    ldr     r2, =__iwram_end
1:  cmp     r1, r2
    ldrlo   r3, [r0], #4
    strlo   r3, [r1], #4
    blo     1b

    @ Clear .bss.
    ldr     r1, =__bss_start
    ldr     r2, =__bss_end
    mov     r3, #0
2:  cmp     r1, r2
    strlo   r3, [r1], #4
    blo     2b

    ldr     r0, =main
    mov     lr, pc
    bx      r0
3:  b       3b
    .ltorg

@ Interrupt handler, called by the BIOS dispatcher through 0x03007FFC.
@ Acknowledges the interrupt and raises the BIOS flag that IntrWait checks.
    .section .iwram, "ax"
    .arm
    .global irq_handler
    .type   irq_handler, %function
irq_handler:
    mov     r2, #0x04000000
    add     r2, r2, #0x200
    ldr     r1, [r2]                @ IE | IF << 16
    and     r1, r1, r1, lsr #16     @ IE & IF
    strh    r1, [r2, #2]            @ acknowledge
    ldr     r3, =0x03007FF8
    ldrh    r0, [r3]
    orr     r0, r0, r1
    strh    r0, [r3]
    bx      lr
    .ltorg

@ SRAM type tag: lets emulators pick the right save type without guessing.
    .section .rodata
    .align 2
    .ascii "SRAM_V113"
    .align 2
