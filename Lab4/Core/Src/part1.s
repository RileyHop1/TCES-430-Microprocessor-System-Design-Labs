
.syntax unified
.cpu cortex-m7
.thumb

.equ GPIO_MODER,    0x00
.equ GPIO_ODR,      0x14

.equ RCC_BASE,      0x40023800
.equ RCC_AHB1ENR,   0x30
.equ RCC_GPIOGEN,   (1 << 6)

.equ GPIOG_BASE,    0x40021800
.equ GPIO_BSRR,     0x18

.equ RED_LED,       (1 << 6)  // PG6,
.equ YELLOW_LED,    (1 << 7)  // PG7,

.section .data
.align 2

.global ARRAY_POSITIVE
.global ARRAY_NEGATIVE

ARRAY_POSITIVE:
    .word 45, -12, 87, -34, 16
    .word -98, 120, 53, -7, 64

ARRAY_NEGATIVE:
    .word -45, -12, -87, -34, -16
    .word -98, -120, -53, -7, -64

.section .text
.global part1
.align 2
.thumb_func

part1:
    PUSH {R4, R5, R6, LR}

    LDR R1, [R0]
    MOVS R2, #1
    MOVS R3, #4
    MOVS R6, #10

PART1_LOOP:
    CMP R2, R6
    BGE PART1_LOOP_END

    MUL R4, R2, R3
    ADD R4, R4, R0
    LDR R5, [R4]

    CMP R5, R1
    BLT PART1_ASSIGN_SKIP
    MOV R1, R5

PART1_ASSIGN_SKIP:
    ADDS R2, R2, #1
    B PART1_LOOP

PART1_LOOP_END:
    MOV R0, R1

    // Enable GPIOG clock
    LDR R4, =RCC_BASE
    LDR R5, [R4, #RCC_AHB1ENR]
    ORR R5, R5, #RCC_GPIOGEN
    STR R5, [R4, #RCC_AHB1ENR]

    // Configure PG6 and PG7 as outputs
    LDR R4, =GPIOG_BASE
    LDR R5, [R4, #GPIO_MODER]
    BIC R5, R5, #0xF000
    ORR R5, R5, #0x5000
    STR R5, [R4, #GPIO_MODER]

    CMP R0, #0
    BLT PART1_NEGATIVE
    BGT PART1_POSITIVE

    // Zero case: PG6 LOW, PG7 LOW
    LDR R4, =GPIOG_BASE
    LDR R5, =0x00C00000
    STR R5, [R4, #GPIO_BSRR]
    B PART1_END

PART1_POSITIVE:
    // PG6 HIGH, PG7 LOW
    LDR R4, =GPIOG_BASE
    LDR R5, =0x00800040
    STR R5, [R4, #GPIO_BSRR]
    B PART1_END

PART1_NEGATIVE:
    // PG6 LOW, PG7 HIGH
    LDR R4, =GPIOG_BASE
    LDR R5, =0x00400080
    STR R5, [R4, #GPIO_BSRR]

PART1_END:
    POP {R4, R5, R6, PC}
