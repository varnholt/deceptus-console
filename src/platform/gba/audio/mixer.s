.syntax unified
.cpu arm7tdmi
.arm

@ mixVoice(chunk, voice, count): adds count samples of a voice into a chunk of 32-bit sums, the
@ inner loop of audio.c's mixer. ARM code in IWRAM: the GBA fetches it at one cycle per instruction.
@ the Voice layout is tied to audio.c by its compile-time offset checks. a voice that runs past its
@ end without a loop stops: its data pointer is cleared.
.equ VOICE_DATA, 0
.equ VOICE_POSITION, 4
.equ VOICE_STEP, 8
.equ VOICE_END, 12
.equ VOICE_LOOP_LENGTH, 16
.equ VOICE_VOLUME, 20
.equ POSITION_SHIFT, 12

.section .iwram.mixVoice,"ax",%progbits
.balign 4
.global mixVoice
mixVoice:
    stmfd sp!, {r4-r9}
    ldr r3, [r1, #VOICE_DATA]
    ldr r4, [r1, #VOICE_POSITION]
    ldr r5, [r1, #VOICE_STEP]
    ldr r6, [r1, #VOICE_END]
    ldrb r7, [r1, #VOICE_VOLUME]
1:
    cmp r4, r6
    bhs 3f
2:
    mov r8, r4, lsr #POSITION_SHIFT
    ldrsb r8, [r3, r8]
    ldr r9, [r0]
    mla r9, r8, r7, r9
    str r9, [r0], #4
    add r4, r4, r5
    subs r2, r2, #1
    bne 1b
    str r4, [r1, #VOICE_POSITION]
    ldmfd sp!, {r4-r9}
    bx lr
3:
    ldr r8, [r1, #VOICE_LOOP_LENGTH]
    cmp r8, #0
    beq 5f
4:
    sub r4, r4, r8
    cmp r4, r6
    bhs 4b
    b 2b
5:
    str r8, [r1, #VOICE_DATA]
    ldmfd sp!, {r4-r9}
    bx lr
