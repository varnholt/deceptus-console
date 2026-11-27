.syntax unified
.cpu arm7tdmi
.arm
.section .gba_header,"ax",%progbits
.global _start
_start:
    b reset
    .space 188

.section .text.reset,"ax",%progbits
.balign 4
reset:
    @ initialize separate IRQ and system stacks, leaving interrupts disabled.
    mov r0, #0xd2
    msr cpsr_c, r0
    ldr sp, =0x03007fa0
    mov r0, #0xdf
    msr cpsr_c, r0
    ldr sp, =0x03007f00

    @ standard cartridge waitstates and instruction prefetch.
    ldr r0, =0x04000204
    ldr r1, =0x4317
    strh r1, [r0]

    ldr r0, =__iwram_load
    ldr r1, =__iwram_start
    ldr r2, =__iwram_end
4:
    cmp r1, r2
    ldrlo r3, [r0], #4
    strlo r3, [r1], #4
    blo 4b
    ldr r0, =__data_load
    ldr r1, =__data_start
    ldr r2, =__data_end
1:
    cmp r1, r2
    ldrlo r3, [r0], #4
    strlo r3, [r1], #4
    blo 1b
    mov r0, #0
    ldr r1, =__bss_start
    ldr r2, =__bss_end
2:
    cmp r1, r2
    strlo r0, [r1], #4
    blo 2b
    bl main
3:
    b 3b
.ltorg

@ the BIOS calls the handler at 0x03007ffc in ARM state, in IRQ mode on the small IRQ stack: this one
@ acknowledges the interrupts, then runs audioInterrupt on the system stack with interrupts still off.
.section .text.irq,"ax",%progbits
.arm
.balign 4
.global irqHandler
irqHandler:
    mov r0, #0x04000000
    add r0, r0, #0x200
    ldrh r1, [r0]
    ldrh r2, [r0, #2]
    and r1, r1, r2
    strh r1, [r0, #2]
    ldr r2, =0x03007ff8
    ldrh r3, [r2]
    orr r3, r3, r1
    strh r3, [r2]
    mrs r3, spsr
    stmfd sp!, {r3, lr}
    mov r3, #0x9f
    msr cpsr_c, r3
    stmfd sp!, {lr}
    ldr r3, =audioInterrupt
    mov lr, pc
    bx r3
    ldmfd sp!, {lr}
    mov r3, #0x92
    msr cpsr_c, r3
    ldmfd sp!, {r3, lr}
    msr spsr_cf, r3
    bx lr
.ltorg

@ clears the CPSR's IRQ mask, which Thumb code cannot reach.
.section .text.irqEnable,"ax",%progbits
.arm
.balign 4
.global irqEnable
irqEnable:
    mrs r0, cpsr
    bic r0, r0, #0x80
    msr cpsr_c, r0
    bx lr

@ compiler signed division uses the console BIOS integer service.
.section .text.__aeabi_idiv,"ax",%progbits
.thumb
.balign 4
.global __aeabi_idiv
.thumb_func
__aeabi_idiv:
    swi 0x06
    bx lr

.section .text.__aeabi_uidiv,"ax",%progbits
.thumb
.balign 4
.global __aeabi_uidiv
.thumb_func
__aeabi_uidiv:
    cmp r1, #0
    beq 5f
    movs r2, r0
    orrs r2, r1
    bmi 5f
    swi 0x06
    bx lr
5:
    ldr r2, =deceptusGbaUnsignedDivide
    bx r2
.ltorg
